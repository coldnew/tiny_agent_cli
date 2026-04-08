#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "llmconfig.hpp"
#include "mcp_client.hpp"

class BaseTool {
 public:
  BaseTool(std::string name, std::string description, nlohmann::json parameters)
      : name_(std::move(name)), description_(std::move(description)), parameters_(std::move(parameters)) {}
  virtual ~BaseTool() = default;

  const std::string&    Name()        const { return name_; }
  const std::string&    Description() const { return description_; }
  const nlohmann::json& Parameters()  const { return parameters_; }

  nlohmann::json ToOpenAIFunction() const;

  virtual std::string Execute(const nlohmann::json& kwargs) = 0;

 private:
  std::string    name_;
  std::string    description_;
  nlohmann::json parameters_;
};

class ToolRegistry {
 public:
  explicit ToolRegistry(const std::vector<McpServerConfig>& mcp_servers = {});

  nlohmann::json GetDefinitions() const;
  std::string    Execute(const std::string& name, const std::string& arguments_json) const;
  nlohmann::json GetToolsSummary() const;
  nlohmann::json GetMcpStatus() const;

 private:
  void RegisterMcpTools(const std::vector<McpServerConfig>& mcp_servers);
  void Register(std::unique_ptr<BaseTool> tool);

  std::unordered_map<std::string, std::unique_ptr<BaseTool>> tools_;
  std::vector<std::unique_ptr<McpClient>> mcp_clients_;
  nlohmann::json mcp_status_ = nlohmann::json::array();
};
