#pragma once

#include <string>

struct LlmConfig {
  std::string provider = "openai";
  std::string api_key;
  std::string model    = "gpt-4o-mini";
  std::string base_url;
};

LlmConfig LoadLlmConfig(const std::string& config_path);
