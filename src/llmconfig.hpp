#pragma once

#include <map>
#include <string>
#include <vector>

struct McpServerConfig {
  std::string name;
  std::string command;
  std::vector<std::string> args;
  std::string cwd;
  std::map<std::string, std::string> env;
  bool enabled = true;
};

struct LlmConfig {
  std::string provider = "openai";
  std::string api_key;
  std::string model    = "gpt-4o-mini";
  std::string base_url;
  std::vector<McpServerConfig> mcp_servers;
};

LlmConfig LoadLlmConfig(const std::string& config_path);
