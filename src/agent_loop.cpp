#include "agent_loop.hpp"

#include <map>

AgentLoop::AgentLoop(OpenAIStreamClient* client, ToolRegistry* registry, std::string model)
    : client_(client), registry_(registry), model_(std::move(model)) {}

void AgentLoop::Run(const nlohmann::json& messages,
                    const std::function<void(const nlohmann::json&)>& emit_event) const {
  nlohmann::json current_messages = messages;
  const int max_iterations = 10;

  const nlohmann::json tools_def = registry_->GetDefinitions();

  for (int iteration = 1; iteration <= max_iterations; ++iteration) {
    // Sanitise messages for backend compatibility across vendors.
    nlohmann::json cleaned = nlohmann::json::array();
    for (const auto& m : current_messages) {
      nlohmann::json mm = m;
      if (mm.contains("tool_calls") && mm["tool_calls"].is_array() && mm["tool_calls"].empty())
        mm.erase("tool_calls");
      if (mm.contains("content") && mm["content"].is_string() && mm["content"].get<std::string>().empty())
        mm["content"] = nullptr;
      cleaned.push_back(mm);
    }

    nlohmann::json req = {
        {"model",          model_},
        {"messages",       cleaned},
        {"stream",         true},
        {"stream_options", {{"include_usage", true}}},
    };
    if (!tools_def.empty()) req["tools"] = tools_def;

    nlohmann::json assistant_msg = {{"role", "assistant"}, {"content", ""}};
    std::map<int, nlohmann::json> tool_call_buffer;

    bool ok = client_->StreamChatCompletions(
        req,
        [&](const nlohmann::json& chunk) {
          if (chunk.contains("usage") && !chunk["usage"].is_null()) {
            const auto& usage = chunk["usage"];
            emit_event({
                {"type",              "token_usage"},
                {"prompt_tokens",     usage.value("prompt_tokens", 0)},
                {"completion_tokens", usage.value("completion_tokens", 0)},
                {"total_tokens",      usage.value("total_tokens", 0)},
            });
          }

          if (!chunk.contains("choices") || !chunk["choices"].is_array() || chunk["choices"].empty()) return;
          const auto& delta = chunk["choices"][0].value("delta", nlohmann::json::object());

          if (delta.contains("content") && delta["content"].is_string()) {
            const std::string piece = delta["content"].get<std::string>();
            assistant_msg["content"] = assistant_msg["content"].get<std::string>() + piece;
            emit_event({{"type", "text_delta"}, {"content", piece}});
          }

          if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (const auto& tc : delta["tool_calls"]) {
              int idx = tc.value("index", 0);
              if (!tool_call_buffer.count(idx)) {
                tool_call_buffer[idx] = {
                    {"id",   tc.value("id", "")},
                    {"type", "function"},
                    {"function", {
                        {"name",      tc.contains("function") ? tc["function"].value("name", "")      : ""},
                        {"arguments", tc.contains("function") ? tc["function"].value("arguments", "") : ""},
                    }},
                };
              } else {
                if (tc.contains("id") && tc["id"].is_string())
                  tool_call_buffer[idx]["id"] =
                      tool_call_buffer[idx]["id"].get<std::string>() + tc["id"].get<std::string>();
                if (tc.contains("function")) {
                  if (tc["function"].contains("name") && tc["function"]["name"].is_string())
                    tool_call_buffer[idx]["function"]["name"] =
                        tool_call_buffer[idx]["function"]["name"].get<std::string>() +
                        tc["function"]["name"].get<std::string>();
                  if (tc["function"].contains("arguments") && tc["function"]["arguments"].is_string())
                    tool_call_buffer[idx]["function"]["arguments"] =
                        tool_call_buffer[idx]["function"]["arguments"].get<std::string>() +
                        tc["function"]["arguments"].get<std::string>();
                }
              }
            }
          }
        },
        [&](const std::string& err) { emit_event({{"type", "error"}, {"content", err}}); });

    if (!ok) break;

    if (!tool_call_buffer.empty()) {
      nlohmann::json tool_calls = nlohmann::json::array();
      for (const auto& kv : tool_call_buffer) tool_calls.push_back(kv.second);
      assistant_msg["tool_calls"] = tool_calls;
    }

    if (assistant_msg["content"].is_string() && assistant_msg["content"].get<std::string>().empty())
      assistant_msg["content"] = nullptr;

    current_messages.push_back(assistant_msg);

    if (!assistant_msg.contains("tool_calls")) break;

    for (const auto& tc : assistant_msg["tool_calls"]) {
      const std::string tool_name = tc["function"].value("name", "");
      const std::string tool_args = tc["function"].value("arguments", "{}");
      const std::string tool_id   = tc.value("id", "");

      emit_event({
          {"type",      "tool_call_start"},
          {"id",        tool_id},
          {"name",      tool_name},
          {"arguments", tool_args},
      });

      const std::string result = registry_->Execute(tool_name, tool_args);

      std::string summary = result;
      if (summary.size() > 100) summary = summary.substr(0, 100) + "...";

      emit_event({
          {"type",           "tool_call_end"},
          {"id",             tool_id},
          {"name",           tool_name},
          {"result_summary", summary},
      });

      current_messages.push_back({
          {"role",         "tool"},
          {"tool_call_id", tool_id},
          {"name",         tool_name},
          {"content",      result},
      });
    }
  }

  nlohmann::json new_messages = nlohmann::json::array();
  for (size_t i = messages.size(); i < current_messages.size(); ++i)
    new_messages.push_back(current_messages[i]);

  emit_event({{"type", "turn_end"}, {"new_messages", new_messages}});
}
