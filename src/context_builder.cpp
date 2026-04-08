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

  std::ostringstream out;
  out << "You are tinybot, a helpful AI assistant running as a CLI agent.\n\n";
  out << "## Current Time\n" << time_buf << " (" << tz_buf << ")\n\n";
  out << "## Runtime\nLinux " << sizeof(void*) * 8 << "-bit, C++ CLI agent\n\n";
  out << "## Workspace\n";
  out << "Your workspace is at: " << workspace << "\n";
  out << "- Long-term memory: " << workspace << "/memory/MEMORY.md\n";
  out << "- Output directory: " << workspace << "/outputs/\n";
  out << "- Custom skills:    " << workspace << "/skills/{skill-name}/SKILL.md\n\n";
  out << "> **IMPORTANT:** All generated files (documents, code, data, etc.) MUST be saved under `"
      << workspace << "/outputs/`. Do NOT write files to the workspace root or other locations.\n\n";
  out << "## Tool-Use Guidelines\n";
  out << "- You may briefly state intent before calling a tool, but never predict its output.\n";
  out << "- Never assume a file or directory exists — verify first.\n";
  out << "- Read a file before editing it; re-read after writing if accuracy matters.\n";
  out << "- Analyze errors before trying a different approach.\n\n";
  out << "## Memory\n";
  out << "- Save important facts: write to " << workspace << "/memory/MEMORY.md\n";
  out << "- Recall past events: use `exec` to grep " << workspace << "/memory/HISTORY.md\n";

  return out.str();
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
