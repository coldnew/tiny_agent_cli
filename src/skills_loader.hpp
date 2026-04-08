#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct Skill {
  std::string name;
  std::string description;
  bool active = true;
  bool always_load = false;
  std::string path;
  std::string content;
};

// SkillsLoader discovers and parses skills under workspace/skills.
class SkillsLoader {
 public:
  explicit SkillsLoader(const std::string& workspace_dir);

  void LoadAllSkills();

  // Prompt block for always-loaded skills.
  std::string GetAlwaysSkillsPrompt() const;

  // Prompt summary listing optional skills.
  std::string BuildSkillsSummaryPrompt() const;

  nlohmann::json GetSkillsSummary() const;

 private:
  Skill ParseSkillFile(const std::string& skill_name, const std::string& path) const;

  std::string skills_dir_;
  std::vector<Skill> skills_;
};
