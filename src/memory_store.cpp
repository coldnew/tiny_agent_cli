#include "memory_store.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

MemoryStore::MemoryStore(const std::string& workspace_dir, const std::string& session_id) {
  memory_dir_ = (fs::path(workspace_dir) / "memory").string();
  fs::create_directories(memory_dir_);

  history_file_ = (fs::path(memory_dir_) / (session_id + "_history.json")).string();
  tokens_file_  = (fs::path(memory_dir_) / (session_id + "_tokens.json")).string();
  long_term_file_ = (fs::path(memory_dir_) / "MEMORY.md").string();

  LoadHistory();
  LoadTokens();
}

void MemoryStore::LoadHistory() {
  std::lock_guard<std::mutex> lock(mu_);
  messages_ = nlohmann::json::array();

  std::ifstream in(history_file_);
  if (!in.is_open()) return;

  try {
    in >> messages_;
    if (!messages_.is_array()) messages_ = nlohmann::json::array();
  } catch (...) {
    messages_ = nlohmann::json::array();
  }
}

void MemoryStore::SaveHistory() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::ofstream out(history_file_);
  out << messages_.dump(2);
}

void MemoryStore::LoadTokens() {
  std::lock_guard<std::mutex> lock(mu_);
  tokens_ = nlohmann::json{{"prompt", 0}, {"completion", 0}};

  std::ifstream in(tokens_file_);
  if (!in.is_open()) return;

  try {
    nlohmann::json data;
    in >> data;
    tokens_["prompt"]     = data.value("prompt", 0);
    tokens_["completion"] = data.value("completion", 0);
  } catch (...) {
    tokens_ = nlohmann::json{{"prompt", 0}, {"completion", 0}};
  }
}

void MemoryStore::SaveTokens() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::ofstream out(tokens_file_);
  out << tokens_.dump(2);
}

void MemoryStore::AddMessage(const nlohmann::json& message) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    messages_.push_back(message);
  }
  SaveHistory();
}

nlohmann::json MemoryStore::GetMessages(int window_size) const {
  std::lock_guard<std::mutex> lock(mu_);

  if (!messages_.is_array()) return nlohmann::json::array();
  if (static_cast<int>(messages_.size()) <= window_size) return messages_;

  int start_idx = static_cast<int>(messages_.size()) - window_size;

  // Walk backward to the nearest user message so tool-call chains are not orphaned.
  while (start_idx > 0 && messages_[start_idx].value("role", "") != "user") {
    --start_idx;
  }

  nlohmann::json out = nlohmann::json::array();
  for (int i = start_idx; i < static_cast<int>(messages_.size()); ++i) {
    out.push_back(messages_[i]);
  }
  return out;
}

std::string MemoryStore::GetLongTermMemory() const {
  std::ifstream in(long_term_file_);
  if (!in.is_open()) return "";
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void MemoryStore::SaveLongTermMemory(const std::string& text) {
  std::ofstream out(long_term_file_);
  out << text;
}

void MemoryStore::AddTokens(int prompt_tokens, int completion_tokens) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    tokens_["prompt"]     = tokens_.value("prompt", 0) + prompt_tokens;
    tokens_["completion"] = tokens_.value("completion", 0) + completion_tokens;
  }
  SaveTokens();
}

nlohmann::json MemoryStore::GetTokens() const {
  std::lock_guard<std::mutex> lock(mu_);
  return tokens_;
}

void MemoryStore::ClearHistory() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    messages_ = nlohmann::json::array();
    tokens_   = nlohmann::json{{"prompt", 0}, {"completion", 0}};
  }
  SaveHistory();
  SaveTokens();
}

nlohmann::json MemoryStore::AllMessages() const {
  std::lock_guard<std::mutex> lock(mu_);
  return messages_;
}
