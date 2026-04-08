#include "mcp_client.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool WriteAll(int fd, const std::string& bytes) {
  const char* p = bytes.data();
  size_t n = bytes.size();
  while (n > 0) {
    const ssize_t w = write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    p += static_cast<size_t>(w);
    n -= static_cast<size_t>(w);
  }
  return true;
}

bool ReadExact(int fd, std::string* out, size_t size, int timeout_ms) {
  out->clear();
  out->reserve(size);
  while (out->size() < size) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    const int pr = poll(&pfd, 1, timeout_ms);
    if (pr == 0) return false;
    if (pr < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    char buf[4096];
    const size_t want = std::min<size_t>(sizeof(buf), size - out->size());
    const ssize_t r = read(fd, buf, want);
    if (r < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (r == 0) return false;
    out->append(buf, static_cast<size_t>(r));
  }
  return true;
}

}  // namespace

McpClient::McpClient(std::string server_name,
                     std::string command,
                     std::vector<std::string> args,
                     std::string cwd,
                     std::map<std::string, std::string> env)
    : server_name_(std::move(server_name)),
      command_(std::move(command)),
      args_(std::move(args)),
      cwd_(std::move(cwd)),
      env_(std::move(env)) {}

McpClient::~McpClient() {
  if (in_fd_ >= 0) close(in_fd_);
  if (out_fd_ >= 0) close(out_fd_);
  if (child_pid_ > 0) {
    kill(child_pid_, SIGTERM);
    waitpid(child_pid_, nullptr, 0);
  }
}

bool McpClient::Connect(std::string* error) {
  if (connected_) return true;
  if (!Spawn(error)) return false;
  if (!Initialize(error)) return false;
  connected_ = true;
  return true;
}

std::vector<nlohmann::json> McpClient::ListTools(std::string* error) {
  nlohmann::json result;
  if (!Request("tools/list", nlohmann::json::object(), &result, error)) return {};
  if (!result.contains("tools") || !result["tools"].is_array()) return {};

  std::vector<nlohmann::json> tools;
  for (const auto& t : result["tools"]) tools.push_back(t);
  return tools;
}

std::string McpClient::CallTool(const std::string& tool_name,
                                const nlohmann::json& args,
                                std::string* error) {
  nlohmann::json result;
  if (!Request("tools/call", {{"name", tool_name}, {"arguments", args}}, &result, error))
    return "";

  if (result.contains("content") && result["content"].is_array()) {
    std::string out;
    for (const auto& item : result["content"]) {
      if (item.is_object() && item.value("type", "") == "text" && item["text"].is_string()) {
        if (!out.empty()) out += "\n";
        out += item["text"].get<std::string>();
      } else {
        if (!out.empty()) out += "\n";
        out += item.dump();
      }
    }
    if (!out.empty()) return out;
  }

  if (result.contains("structuredContent"))
    return result["structuredContent"].dump(2);

  return result.dump(2);
}

bool McpClient::Spawn(std::string* error) {
  int to_child[2] = {-1, -1};
  int from_child[2] = {-1, -1};
  if (pipe(to_child) != 0) {
    if (error) *error = "failed to create pipes";
    return false;
  }
  if (pipe(from_child) != 0) {
    close(to_child[0]); close(to_child[1]);
    if (error) *error = "failed to create pipes";
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    if (error) *error = "failed to fork";
    close(to_child[0]); close(to_child[1]);
    close(from_child[0]); close(from_child[1]);
    return false;
  }

  if (pid == 0) {
    if (!cwd_.empty() && chdir(cwd_.c_str()) != 0) _exit(127);

    for (const auto& kv : env_) setenv(kv.first.c_str(), kv.second.c_str(), 1);

    dup2(to_child[0], STDIN_FILENO);
    dup2(from_child[1], STDOUT_FILENO);

    close(to_child[0]); close(to_child[1]);
    close(from_child[0]); close(from_child[1]);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(command_.c_str()));
    for (auto& a : args_) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    execvp(command_.c_str(), argv.data());
    _exit(127);
  }

  close(to_child[0]);
  close(from_child[1]);

  in_fd_ = to_child[1];
  out_fd_ = from_child[0];
  child_pid_ = static_cast<int>(pid);
  return true;
}

bool McpClient::Initialize(std::string* error) {
  nlohmann::json init_result;
  if (!Request(
          "initialize",
          {
              {"protocolVersion", "2024-11-05"},
              {"clientInfo", {{"name", "tiny_agent_cli"}, {"version", "1.0.0"}}},
              {"capabilities", nlohmann::json::object()},
          },
          &init_result,
          error)) return false;

  if (!Send(
          {
              {"jsonrpc", "2.0"},
              {"method", "notifications/initialized"},
              {"params", nlohmann::json::object()},
          },
          error)) return false;

  return true;
}

bool McpClient::Send(const nlohmann::json& obj, std::string* error) {
  const std::string body = obj.dump();
  std::ostringstream header;
  header << "Content-Length: " << body.size() << "\r\n\r\n";
  if (!WriteAll(in_fd_, header.str()) || !WriteAll(in_fd_, body)) {
    if (error) *error = "failed to write to MCP server";
    return false;
  }
  return true;
}

bool McpClient::ReadMessage(nlohmann::json* out, std::string* error, int timeout_ms) {
  std::string headers;
  while (true) {
    struct pollfd pfd = {.fd = out_fd_, .events = POLLIN, .revents = 0};
    const int pr = poll(&pfd, 1, timeout_ms);
    if (pr == 0) {
      if (error) *error = "timeout waiting for MCP response";
      return false;
    }
    if (pr < 0) {
      if (errno == EINTR) continue;
      if (error) *error = "poll failed while reading MCP response";
      return false;
    }

    char c = 0;
    const ssize_t r = read(out_fd_, &c, 1);
    if (r < 0) {
      if (errno == EINTR) continue;
      if (error) *error = "failed to read MCP response";
      return false;
    }
    if (r == 0) {
      if (error) *error = "MCP server closed the stream";
      return false;
    }
    headers.push_back(c);
    if (headers.size() >= 4 &&
        headers.compare(headers.size() - 4, 4, "\r\n\r\n") == 0) {
      break;
    }
  }

  std::istringstream hs(headers);
  std::string line;
  size_t content_length = 0;
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;
    static const std::string kPrefix = "Content-Length:";
    if (line.rfind(kPrefix, 0) == 0) {
      std::string n = line.substr(kPrefix.size());
      try {
        content_length = static_cast<size_t>(std::stoul(n));
      } catch (...) {
        if (error) *error = "invalid Content-Length header in MCP response";
        return false;
      }
    }
  }
  if (content_length == 0) {
    if (error) *error = "invalid MCP message framing";
    return false;
  }

  std::string body;
  if (!ReadExact(out_fd_, &body, content_length, timeout_ms)) {
    if (error) *error = "failed to read MCP message body";
    return false;
  }

  try {
    *out = nlohmann::json::parse(body);
    return true;
  } catch (const std::exception& e) {
    if (error) *error = std::string("failed to parse MCP JSON: ") + e.what();
    return false;
  }
}

bool McpClient::Request(const std::string& method,
                        const nlohmann::json& params,
                        nlohmann::json* result,
                        std::string* error,
                        int timeout_ms) {
  if (!connected_ && method != "initialize") {
    if (error) *error = "MCP server is not connected";
    return false;
  }

  const int id = next_id_++;
  if (!Send(
          {
              {"jsonrpc", "2.0"},
              {"id", id},
              {"method", method},
              {"params", params},
          },
          error)) return false;

  while (true) {
    nlohmann::json msg;
    if (!ReadMessage(&msg, error, timeout_ms)) return false;

    if (!msg.contains("id")) {
      continue;  // notification
    }

    if (msg["id"] != id) {
      continue;  // ignore out-of-order responses for this simple client
    }

    if (msg.contains("error")) {
      if (error) *error = "MCP error: " + msg["error"].dump();
      return false;
    }

    if (!msg.contains("result")) {
      if (error) *error = "MCP response missing result";
      return false;
    }

    *result = msg["result"];
    return true;
  }
}
