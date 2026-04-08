#include "context_builder.hpp"

#include <ctime>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

ContextBuilder::ContextBuilder(MemoryStore* memory, SkillsLoader* skills, const std::string& workspace_dir)
    : memory_(memory), skills_(skills), workspace_dir_(workspace_dir) {}

std::string ContextBuilder::GetIdentityBlock() const {
  std::time_t now = std::time(nullptr);
  std::tm local_tm = *std::localtime(&now);

  char time_buf[64] = {};
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local_tm);

  char tz_buf[32] = {};
  std::strftime(tz_buf, sizeof(tz_buf), "%Z", &local_tm);

  const std::string workspace = fs::absolute(workspace_dir_).generic_string();

  std::string prompt = R"(You are tinybot, a helpful AI assistant running as a CLI agent.

## Current Time
{TIME} ({TZ})

## Runtime
Linux {BITS}-bit, C++ CLI agent

## Workspace
Your workspace is at: {WORKSPACE}
- Long-term memory: {WORKSPACE}/memory/MEMORY.md
- Output directory: {WORKSPACE}/outputs/
- Custom skills:    {WORKSPACE}/skills/{skill-name}/SKILL.md

> **IMPORTANT:** All generated files (documents, code, data, etc.) MUST be saved under `{WORKSPACE}/outputs/`. Do NOT write files to the workspace root or other locations.

## Tool-Use Guidelines
- You may briefly state intent before calling a tool, but never predict its output.
- Never assume a file or directory exists — verify first.
- Read a file before editing it; re-read after writing if accuracy matters.
- Analyze errors before trying a different approach.

## Memory
- Save important facts: write to {WORKSPACE}/memory/MEMORY.md
- Recall past events: use `exec` to grep {WORKSPACE}/memory/HISTORY.md
)";

  const auto replace_all = [](std::string* s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s->find(from, pos)) != std::string::npos) {
      s->replace(pos, from.size(), to);
      pos += to.size();
    }
  };

  replace_all(&prompt, "{TIME}", time_buf);
  replace_all(&prompt, "{TZ}", tz_buf);
  replace_all(&prompt, "{BITS}", std::to_string(sizeof(void*) * 8));
  replace_all(&prompt, "{WORKSPACE}", workspace);

  return prompt;
}

std::string ContextBuilder::BuildSystemPrompt() const {
  std::vector<std::string> parts;
  parts.push_back(GetIdentityBlock());

  const std::string always_skills = skills_->GetAlwaysSkillsPrompt();
  if (!always_skills.empty()) parts.push_back(always_skills);

  const std::string long_term = memory_->GetLongTermMemory();
  if (!long_term.empty()) parts.push_back("# Working Memory\n\n" + long_term);

  const std::string summary = skills_->BuildSkillsSummaryPrompt();
  if (!summary.empty()) parts.push_back(summary);

  std::ostringstream out;
  for (size_t i = 0; i < parts.size(); ++i) {
    out << parts[i];
    if (i + 1 < parts.size()) out << "\n\n---\n\n";
  }
  return out.str();
}

nlohmann::json ContextBuilder::BuildMessages(const std::string& user_message) const {
  nlohmann::json messages = nlohmann::json::array();

  messages.push_back({{"role", "system"}, {"content", BuildSystemPrompt()}});

  for (const auto& m : memory_->GetMessages(20)) messages.push_back(m);

  messages.push_back({{"role", "user"}, {"content", user_message}});
  return messages;
}
