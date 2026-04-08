#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "agent_loop.hpp"
#include "context_builder.hpp"
#include "memory_store.hpp"
#include "openai_stream_client.hpp"
#include "skills_loader.hpp"
#include "tools.hpp"

// TinyAgent composes all subsystems and exposes a simple chat interface.
class TinyAgent {
 public:
  TinyAgent(const std::string& workspace_dir,
            const std::string& api_key,
            const std::string& base_url,
            const std::string& model,
            const std::vector<McpServerConfig>& mcp_servers = {});

  // Run one chat turn, calling emit_event for each streaming event.
  void ChatStream(const std::string& user_message,
                  const std::function<void(const nlohmann::json&)>& emit_event,
                  const std::function<bool()>& should_cancel = nullptr);

  nlohmann::json GetSkillsSummary();
  nlohmann::json GetToolsSummary() const;
  nlohmann::json GetMcpStatus() const;
  nlohmann::json GetTokens() const;
  nlohmann::json GetAllMessages() const;
  std::string    GetLongTermMemory() const;
  void           ClearMemory();

 private:
  std::string workspace_dir_;

  MemoryStore        memory_;
  SkillsLoader       skills_;
  ToolRegistry       tools_;
  ContextBuilder     context_;
  OpenAIStreamClient client_;
  AgentLoop          loop_;
};
