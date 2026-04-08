#include "tiny_agent.hpp"

#include <stdexcept>

TinyAgent::TinyAgent(const std::string& workspace_dir,
                     const std::string& api_key,
                     const std::string& base_url,
                     const std::string& model)
    : workspace_dir_(workspace_dir),
      memory_(workspace_dir),
      skills_(workspace_dir),
      tools_(),
      context_(&memory_, &skills_, workspace_dir),
      client_(api_key, base_url),
      loop_(&client_, &tools_, model) {
  if (api_key.empty())
    throw std::runtime_error(
        "No API key provided. Set OPENAI_API_KEY / OPENROUTER_API_KEY or add llm.api_key to config.yaml.");
}

void TinyAgent::ChatStream(const std::string& user_message,
                           const std::function<void(const nlohmann::json&)>& emit_event,
                           const std::function<bool()>& should_cancel) {
  const nlohmann::json payload = context_.BuildMessages(user_message);

  memory_.AddMessage({{"role", "user"}, {"content", user_message}});

  loop_.Run(payload, [&](const nlohmann::json& event) {
    const std::string type = event.value("type", "");

    if (type == "turn_end") {
      if (event.contains("new_messages") && event["new_messages"].is_array()) {
        for (const auto& msg : event["new_messages"])
          memory_.AddMessage(msg);
      }
      return;
    }

    if (type == "token_usage")
      memory_.AddTokens(event.value("prompt_tokens", 0), event.value("completion_tokens", 0));

    emit_event(event);
  }, should_cancel);
}

nlohmann::json TinyAgent::GetSkillsSummary() {
  skills_.LoadAllSkills();
  return skills_.GetSkillsSummary();
}

nlohmann::json TinyAgent::GetToolsSummary() const  { return tools_.GetToolsSummary(); }
nlohmann::json TinyAgent::GetTokens() const        { return memory_.GetTokens(); }
nlohmann::json TinyAgent::GetAllMessages() const   { return memory_.AllMessages(); }
std::string    TinyAgent::GetLongTermMemory() const { return memory_.GetLongTermMemory(); }
void           TinyAgent::ClearMemory()            { memory_.ClearHistory(); }
