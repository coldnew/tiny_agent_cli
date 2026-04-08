#include "llmconfig.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>

#include <yaml-cpp/yaml.h>

namespace {
namespace fs = std::filesystem;

std::string ToLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

LlmConfig ResolveLlmConfig(const YAML::Node& root) {
  LlmConfig cfg;
  cfg.base_url = "https://api.openai.com/v1";

  if (root["llm"]) {
    YAML::Node llm = root["llm"];
    if (llm["provider"]) cfg.provider = ToLower(llm["provider"].as<std::string>());
    if (llm["api_key"])  cfg.api_key  = llm["api_key"].as<std::string>();
    if (llm["model"])    cfg.model    = llm["model"].as<std::string>();
    if (llm["base_url"]) cfg.base_url = llm["base_url"].as<std::string>();
  }

  // Apply provider default endpoint only when base_url wasn't explicitly overridden.
  if (cfg.provider == "openrouter" && cfg.base_url == "https://api.openai.com/v1")
    cfg.base_url = "https://openrouter.ai/api/v1";

  // Alias from OpenRouter docs:
  // https://openrouter.ai/docs/guides/routing/routers/free-models-router
  if (cfg.provider == "openrouter" &&
      (cfg.model == "free" || cfg.model == "free-models-router"))
    cfg.model = "openrouter/free";

  if (cfg.api_key.empty()) {
    const char* env = nullptr;
    if (cfg.provider == "openrouter") env = std::getenv("OPENROUTER_API_KEY");
    if (!env) env = std::getenv("OPENAI_API_KEY");
    if (env) cfg.api_key = env;
  }

  return cfg;
}

}  // namespace

LlmConfig LoadLlmConfig(const std::string& config_path) {
  if (!fs::exists(config_path))
    return ResolveLlmConfig(YAML::Node{});

  YAML::Node cfg = YAML::LoadFile(config_path);
  return ResolveLlmConfig(cfg);
}
