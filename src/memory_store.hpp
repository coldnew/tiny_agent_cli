#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// MemoryStore persists short-term conversation history and token usage on disk.
class MemoryStore {
 public:
  explicit MemoryStore(const std::string& workspace_dir, const std::string& session_id = "default");

  void AddMessage(const nlohmann::json& message);

  // Return a safe history window that avoids breaking tool-call chains.
  nlohmann::json GetMessages(int window_size = 20) const;

  std::string GetLongTermMemory() const;
  void SaveLongTermMemory(const std::string& text);

  void AddTokens(int prompt_tokens, int completion_tokens);
  nlohmann::json GetTokens() const;

  void ClearHistory();
  nlohmann::json AllMessages() const;

 private:
  void LoadHistory();
  void SaveHistory() const;
  void LoadTokens();
  void SaveTokens() const;

  std::string memory_dir_;
  std::string history_file_;
  std::string tokens_file_;
  std::string long_term_file_;

  nlohmann::json messages_;
  nlohmann::json tokens_;

  mutable std::mutex mu_;
};
