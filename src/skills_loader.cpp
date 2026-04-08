#include "skills_loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace {
std::string ReadAllText(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) return "";
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> SplitLines(const std::string& s) {
  std::vector<std::string> lines;
  std::stringstream ss(s);
  std::string line;
  while (std::getline(ss, line)) lines.push_back(line);
  return lines;
}
}  // namespace

SkillsLoader::SkillsLoader(const std::string& workspace_dir) {
  skills_dir_ = (fs::path(workspace_dir) / "skills").string();
  LoadAllSkills();
}

void SkillsLoader::LoadAllSkills() {
  skills_.clear();
  fs::create_directories(skills_dir_);

  for (const auto& entry : fs::recursive_directory_iterator(skills_dir_)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().filename() != "SKILL.md") continue;

    const std::string skill_name = entry.path().parent_path().filename().string();
    skills_.push_back(ParseSkillFile(skill_name, entry.path().string()));
  }
}

Skill SkillsLoader::ParseSkillFile(const std::string& skill_name, const std::string& path) const {
  Skill skill;
  skill.name        = skill_name;
  skill.description = "no description";
  skill.path        = fs::path(path).generic_string();

  const std::string text = ReadAllText(path);
  auto lines = SplitLines(text);

  std::string yaml_text;
  std::string markdown_text = text;

  if (!lines.empty() && lines[0] == "---") {
    size_t end_idx = std::string::npos;
    for (size_t i = 1; i < lines.size(); ++i) {
      if (lines[i] == "---") { end_idx = i; break; }
    }

    if (end_idx != std::string::npos) {
      for (size_t i = 1; i < end_idx; ++i) yaml_text += lines[i] + "\n";

      markdown_text.clear();
      for (size_t i = end_idx + 1; i < lines.size(); ++i) {
        markdown_text += lines[i];
        if (i + 1 < lines.size()) markdown_text += "\n";
      }

      try {
        YAML::Node meta = YAML::Load(yaml_text);
        if (meta["description"]) skill.description = meta["description"].as<std::string>();
        if (meta["active"])      skill.active       = meta["active"].as<bool>();
        if (meta["always_load"]) skill.always_load  = meta["always_load"].as<bool>();
      } catch (...) {}
    }
  }

  skill.content = markdown_text;
  return skill;
}

std::string SkillsLoader::GetAlwaysSkillsPrompt() const {
  std::ostringstream out;
  bool has_any = false;

  for (const auto& skill : skills_) {
    if (!skill.active || !skill.always_load) continue;
    if (!has_any) {
      out << "# Always-loaded Skills\n\n";
      has_any = true;
    }
    out << "## Skill: " << skill.name << "\n" << skill.content << "\n\n";
  }

  return out.str();
}

std::string SkillsLoader::BuildSkillsSummaryPrompt() const {
  std::ostringstream out;
  bool has_any = false;

  for (const auto& skill : skills_) {
    if (!skill.active || skill.always_load) continue;
    if (!has_any) {
      out << "# Available Skills\n";
      out << "The following skills extend your capabilities. Use `read_file` on the skill path to learn usage before invoking.\n\n";
      has_any = true;
    }
    out << "- **" << skill.name << "**: " << skill.description << "\n";
    out << "  > Skill guide: `" << skill.path << "`\n";
  }

  return out.str();
}

nlohmann::json SkillsLoader::GetSkillsSummary() const {
  nlohmann::json out = nlohmann::json::array();
  for (const auto& skill : skills_) {
    out.push_back({{"name", skill.name}, {"description", skill.description}, {"active", skill.active}});
  }
  return out;
}
