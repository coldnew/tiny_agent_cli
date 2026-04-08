#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class McpClient {
 public:
  McpClient(std::string server_name,
            std::string command,
            std::vector<std::string> args,
            std::string cwd,
            std::map<std::string, std::string> env);
  ~McpClient();

  bool Connect(std::string* error);
  std::vector<nlohmann::json> ListTools(std::string* error);
  std::string CallTool(const std::string& tool_name,
                       const nlohmann::json& args,
                       std::string* error);

  const std::string& ServerName() const { return server_name_; }

 private:
  bool Spawn(std::string* error);
  bool Initialize(std::string* error);
  bool Send(const nlohmann::json& obj, std::string* error);
  bool ReadMessage(nlohmann::json* out, std::string* error, int timeout_ms);
  bool Request(const std::string& method,
               const nlohmann::json& params,
               nlohmann::json* result,
               std::string* error,
               int timeout_ms = 10000);

  std::string server_name_;
  std::string command_;
  std::vector<std::string> args_;
  std::string cwd_;
  std::map<std::string, std::string> env_;

  int in_fd_ = -1;   // write to child stdin
  int out_fd_ = -1;  // read from child stdout
  int next_id_ = 1;
  int child_pid_ = -1;
  bool connected_ = false;
};
