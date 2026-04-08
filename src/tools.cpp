#include "tools.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>

#include <sys/wait.h>

namespace fs = std::filesystem;

namespace {

std::string ReadTextFile(const std::string& path, size_t max_chars = 10000) {
  std::ifstream in(path);
  if (!in.is_open()) return "Error: cannot open file";

  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (content.size() > max_chars) {
    return content.substr(0, max_chars) + "\n...[truncated]";
  }
  return content;
}

std::string ShellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else           out.push_back(c);
  }
  out += "'";
  return out;
}

std::string SanitizeToolName(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '_') out.push_back(c);
    else out.push_back('_');
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty()) out = "tool";
  if (std::isdigit(static_cast<unsigned char>(out[0])))
    out = "t_" + out;
  return out;
}

// ---------------------------------------------------------------------------

class ReadFileTool : public BaseTool {
 public:
  ReadFileTool()
      : BaseTool("read_file", "Read the contents of a file. Large files may be truncated.",
                 nlohmann::json{
                     {"type", "object"},
                     {"properties", {{"path", {{"type", "string"}, {"description", "Absolute or relative path to the file"}}}}},
                     {"required", nlohmann::json::array({"path"})}}) {}

  std::string Execute(const nlohmann::json& kwargs) override {
    if (!kwargs.contains("path") || !kwargs["path"].is_string())
      return "Error: missing 'path' argument";
    return ReadTextFile(kwargs["path"].get<std::string>());
  }
};

class WriteFileTool : public BaseTool {
 public:
  WriteFileTool()
      : BaseTool("write_file", "Write content to a file, creating it if it does not exist (overwrites).",
                 nlohmann::json{
                     {"type", "object"},
                     {"properties",
                      {{"path",    {{"type", "string"}, {"description", "Destination file path"}}},
                       {"content", {{"type", "string"}, {"description", "Text content to write"}}}}},
                     {"required", nlohmann::json::array({"path", "content"})}}) {}

  std::string Execute(const nlohmann::json& kwargs) override {
    try {
      if (!kwargs.contains("path") || !kwargs.contains("content"))
        return "Error: missing required arguments";

      const std::string path    = kwargs["path"].get<std::string>();
      const std::string content = kwargs["content"].get<std::string>();

      fs::path p(path);
      if (p.has_parent_path()) fs::create_directories(p.parent_path());

      std::ofstream out(path);
      if (!out.is_open()) return "Error: cannot open destination file";

      out << content;
      return "Written: " + path;
    } catch (const std::exception& e) {
      return std::string("Error writing file: ") + e.what();
    }
  }
};

class EditFileTool : public BaseTool {
 public:
  EditFileTool()
      : BaseTool("edit_file", "Edit a file by replacing old_str with new_str. Read the file first to confirm content.",
                 nlohmann::json{
                     {"type", "object"},
                     {"properties",
                      {{"path",    {{"type", "string"}, {"description", "File path"}}},
                       {"old_str", {{"type", "string"}, {"description", "String to replace"}}},
                       {"new_str", {{"type", "string"}, {"description", "Replacement string"}}}}},
                     {"required", nlohmann::json::array({"path", "old_str", "new_str"})}}) {}

  std::string Execute(const nlohmann::json& kwargs) override {
    try {
      if (!kwargs.contains("path") || !kwargs.contains("old_str") || !kwargs.contains("new_str"))
        return "Error: missing required arguments";

      const std::string path    = kwargs["path"].get<std::string>();
      const std::string old_str = kwargs["old_str"].get<std::string>();
      const std::string new_str = kwargs["new_str"].get<std::string>();

      if (!fs::exists(path)) return "Error: file not found: " + path;

      std::string content = ReadTextFile(path, std::numeric_limits<size_t>::max());
      if (content.find(old_str) == std::string::npos)
        return "Error: old_str not found in file";

      size_t pos = 0;
      while ((pos = content.find(old_str, pos)) != std::string::npos) {
        content.replace(pos, old_str.size(), new_str);
        pos += new_str.size();
      }

      std::ofstream out(path);
      if (!out.is_open()) return "Error: cannot write back to file";
      out << content;

      return "Edited: " + path;
    } catch (const std::exception& e) {
      return std::string("Error editing file: ") + e.what();
    }
  }
};

class ShellTool : public BaseTool {
 public:
  explicit ShellTool(int timeout_sec)
      : BaseTool("exec", "Execute a shell command and return its output. Use with care.",
                 nlohmann::json{
                     {"type", "object"},
                     {"properties",
                      {{"command",     {{"type", "string"}, {"description", "Shell command to run"}}},
                       {"working_dir", {{"type", "string"}, {"description", "Optional working directory"}}}}},
                     {"required", nlohmann::json::array({"command"})}}),
        timeout_sec_(timeout_sec) {
    deny_patterns_ = {
        R"(\brm\s+-[rf]{1,2}\b)",
        R"(\bdel\s+/[fq]\b)",
        R"(\brmdir\s+/s\b)",
        R"((?:^|[;&|]\s*)format\b)",
        R"(\b(mkfs|diskpart)\b)",
        R"(\bdd\s+if=)",
        R"(>\s*/dev/sd)",
        R"(\b(shutdown|reboot|poweroff)\b)",
        R"(:\(\)\s*\{.*\};\s*:)",
    };
  }

  std::string Execute(const nlohmann::json& kwargs) override {
    if (!kwargs.contains("command") || !kwargs["command"].is_string())
      return "Error: missing 'command' argument";

    const std::string command = kwargs["command"].get<std::string>();
    std::string lower = command;
    for (char& c : lower) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

    for (const auto& pattern : deny_patterns_) {
      if (std::regex_search(lower, std::regex(pattern)))
        return "Error: command blocked by safety policy";
    }

    std::string cwd = fs::current_path().string();
    if (kwargs.contains("working_dir") && kwargs["working_dir"].is_string())
      cwd = kwargs["working_dir"].get<std::string>();

    const std::string inner    = "cd " + ShellQuote(cwd) + " && " + command + " 2>&1";
    const std::string full_cmd = "timeout " + std::to_string(timeout_sec_) + "s bash -lc " + ShellQuote(inner);

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return "Error: cannot start subprocess";

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      output += buffer;
      if (output.size() > 10000) {
        output = output.substr(0, 10000) + "\n... (output truncated)";
        break;
      }
    }

    int status    = pclose(pipe);
    int exit_code = -1;
    if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);

    if (output.empty()) output = "(no output)";

    if (exit_code == 124)
      return "Error: command timed out (" + std::to_string(timeout_sec_) + "s)";

    if (exit_code != 0)
      output += "\nExit code: " + std::to_string(exit_code);

    return output;
  }

 private:
  int timeout_sec_;
  std::vector<std::string> deny_patterns_;
};

class McpTool : public BaseTool {
 public:
  McpTool(std::string local_name,
          std::string description,
          nlohmann::json parameters,
          McpClient* client,
          std::string remote_tool_name)
      : BaseTool(std::move(local_name), std::move(description), std::move(parameters)),
        client_(client),
        remote_tool_name_(std::move(remote_tool_name)) {}

  std::string Execute(const nlohmann::json& kwargs) override {
    std::string err;
    const std::string result = client_->CallTool(remote_tool_name_, kwargs, &err);
    if (!err.empty()) return "Error: " + err;
    return result;
  }

 private:
  McpClient* client_;
  std::string remote_tool_name_;
};

}  // namespace

// ---------------------------------------------------------------------------

nlohmann::json BaseTool::ToOpenAIFunction() const {
  return {
      {"type", "function"},
      {"function", {{"name", Name()}, {"description", Description()}, {"parameters", Parameters()}}},
  };
}

ToolRegistry::ToolRegistry(const std::vector<McpServerConfig>& mcp_servers) {
  Register(std::make_unique<ReadFileTool>());
  Register(std::make_unique<WriteFileTool>());
  Register(std::make_unique<EditFileTool>());
  Register(std::make_unique<ShellTool>(60));
  RegisterMcpTools(mcp_servers);
}

void ToolRegistry::Register(std::unique_ptr<BaseTool> tool) {
  tools_[tool->Name()] = std::move(tool);
}

void ToolRegistry::RegisterMcpTools(const std::vector<McpServerConfig>& mcp_servers) {
  for (const auto& server : mcp_servers) {
    nlohmann::json server_status = {
        {"name", server.name},
        {"enabled", server.enabled},
        {"connected", false},
        {"tools", nlohmann::json::array()},
    };
    if (!server.command.empty()) server_status["command"] = server.command;

    if (!server.enabled) {
      server_status["error"] = "disabled";
      mcp_status_.push_back(server_status);
      continue;
    }

    auto client = std::make_unique<McpClient>(
        server.name, server.command, server.args, server.cwd, server.env);

    std::string err;
    if (!client->Connect(&err)) {
      server_status["error"] = err;
      mcp_status_.push_back(server_status);
      continue;
    }
    server_status["connected"] = true;

    const std::vector<nlohmann::json> tools = client->ListTools(&err);
    if (!err.empty()) {
      server_status["error"] = err;
      mcp_status_.push_back(server_status);
      continue;
    }

    McpClient* raw_client = client.get();
    mcp_clients_.push_back(std::move(client));

    for (const auto& t : tools) {
      if (!t.is_object() || !t.contains("name") || !t["name"].is_string()) continue;

      const std::string remote_name = t["name"].get<std::string>();
      const std::string local_name =
          "mcp_" + SanitizeToolName(server.name) + "__" + SanitizeToolName(remote_name);

      nlohmann::json params = {
          {"type", "object"},
          {"properties", nlohmann::json::object()},
      };
      if (t.contains("inputSchema") && t["inputSchema"].is_object())
        params = t["inputSchema"];

      std::string description = "MCP tool from server '" + server.name + "'";
      if (t.contains("description") && t["description"].is_string())
        description = t["description"].get<std::string>();

      Register(std::make_unique<McpTool>(
          local_name,
          description,
          params,
          raw_client,
          remote_name));

      server_status["tools"].push_back({
          {"remote_name", remote_name},
          {"local_name", local_name},
      });
    }

    mcp_status_.push_back(server_status);
  }
}

nlohmann::json ToolRegistry::GetDefinitions() const {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& kv : tools_) arr.push_back(kv.second->ToOpenAIFunction());
  return arr;
}

std::string ToolRegistry::Execute(const std::string& name, const std::string& arguments_json) const {
  auto it = tools_.find(name);
  if (it == tools_.end()) return "Error: unknown tool '" + name + "'";

  try {
    nlohmann::json args = nlohmann::json::parse(arguments_json);
    return it->second->Execute(args);
  } catch (const nlohmann::json::parse_error&) {
    return "Error: arguments are not valid JSON";
  } catch (const std::exception& e) {
    return std::string("Error executing tool '") + name + "': " + e.what();
  }
}

nlohmann::json ToolRegistry::GetToolsSummary() const {
  nlohmann::json out = nlohmann::json::array();
  for (const auto& kv : tools_)
    out.push_back({{"name", kv.second->Name()}, {"description", kv.second->Description()}});
  return out;
}

nlohmann::json ToolRegistry::GetMcpStatus() const {
  return mcp_status_;
}
