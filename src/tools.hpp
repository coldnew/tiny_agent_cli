#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

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
  ToolRegistry();

  nlohmann::json GetDefinitions() const;
  std::string    Execute(const std::string& name, const std::string& arguments_json) const;
  nlohmann::json GetToolsSummary() const;

 private:
  void Register(std::unique_ptr<BaseTool> tool);

  std::unordered_map<std::string, std::unique_ptr<BaseTool>> tools_;
};
