#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

// OpenAIStreamClient makes streaming chat-completion calls via libcurl.
class OpenAIStreamClient {
 public:
  OpenAIStreamClient(std::string api_key, std::string base_url);

  // Stream one chat completion request.
  // on_chunk is called for each parsed JSON object from SSE "data:" lines.
  // on_error is called with an error message if the request fails.
  bool StreamChatCompletions(
      const nlohmann::json& request_body,
      const std::function<void(const nlohmann::json&)>& on_chunk,
      const std::function<void(const std::string&)>& on_error) const;

 private:
  std::string api_key_;
  std::string base_url_;
};
