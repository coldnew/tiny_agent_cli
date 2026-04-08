#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "openai_stream_client.hpp"
#include "tools.hpp"

// AgentLoop drives one end-to-end model/tool interaction turn.
class AgentLoop {
 public:
  AgentLoop(OpenAIStreamClient* client, ToolRegistry* registry, std::string model);

  void Run(
      const nlohmann::json& messages,
      const std::function<void(const nlohmann::json&)>& emit_event,
      const std::function<bool()>& should_cancel = nullptr) const;

 private:
  OpenAIStreamClient* client_;
  ToolRegistry*       registry_;
  std::string         model_;
};
