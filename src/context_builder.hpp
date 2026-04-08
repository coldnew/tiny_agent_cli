#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "memory_store.hpp"
#include "skills_loader.hpp"

// ContextBuilder assembles model input: system prompt + history + user message.
class ContextBuilder {
 public:
  ContextBuilder(MemoryStore* memory, SkillsLoader* skills, const std::string& workspace_dir);

  std::string BuildSystemPrompt() const;
  nlohmann::json BuildMessages(const std::string& user_message) const;

 private:
  std::string GetIdentityBlock() const;

  MemoryStore*  memory_;
  SkillsLoader* skills_;
  std::string   workspace_dir_;
};
