#include <csignal>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "tiny_agent.hpp"

namespace fs = std::filesystem;

// ANSI colour helpers (disabled automatically when not a TTY by the caller).
namespace color {
static bool enabled = true;
static const char* reset  = "\033[0m";
static const char* bold   = "\033[1m";
static const char* dim    = "\033[2m";
static const char* cyan   = "\033[36m";
static const char* yellow = "\033[33m";
static const char* red    = "\033[31m";
static const char* green  = "\033[32m";

std::string wrap(const char* code, const std::string& text) {
  if (!enabled) return text;
  return std::string(code) + text + reset;
}
}  // namespace color

static volatile bool g_interrupted = false;

static void SigintHandler(int) {
  g_interrupted = true;
}

static std::string ToLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

struct LlmConfig {
  std::string provider = "openai";
  std::string api_key;
  std::string model    = "gpt-4o-mini";
  std::string base_url;
};

static LlmConfig ResolveLlmConfig(const YAML::Node& root) {
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

static void PrintHelp() {
  std::cout
      << color::wrap(color::bold, "Commands:\n")
      << "  /help     Show this help\n"
      << "  /clear    Clear conversation history\n"
      << "  /status   Show loaded skills and tools\n"
      << "  /memory   Show long-term memory\n"
      << "  /tokens   Show cumulative token usage\n"
      << "  /history  Show conversation history\n"
      << "  /quit     Exit\n"
      << "\n"
      << "Ctrl-C during a response cancels output (but the turn still completes internally).\n"
      << "Ctrl-D or /quit to exit.\n";
}

static void PrintStatus(TinyAgent& agent) {
  const auto skills = agent.GetSkillsSummary();
  const auto tools  = agent.GetToolsSummary();

  std::cout << color::wrap(color::bold, "Skills:\n");
  if (skills.empty()) {
    std::cout << "  (none)\n";
  } else {
    for (const auto& s : skills) {
      std::cout << "  " << color::wrap(color::cyan, s.value("name", "?"))
                << " — " << s.value("description", "")
                << (s.value("active", true) ? "" : " [inactive]") << "\n";
    }
  }

  std::cout << color::wrap(color::bold, "Tools:\n");
  for (const auto& t : tools) {
    std::cout << "  " << color::wrap(color::green, t.value("name", "?"))
              << " — " << t.value("description", "") << "\n";
  }
}

static void PrintMemory(TinyAgent& agent) {
  const std::string ltm = agent.GetLongTermMemory();
  if (ltm.empty()) {
    std::cout << "(long-term memory is empty)\n";
  } else {
    std::cout << ltm << "\n";
  }
}

static void PrintTokens(TinyAgent& agent) {
  const auto tok = agent.GetTokens();
  std::cout << "Prompt tokens: "     << tok.value("prompt", 0)     << "\n";
  std::cout << "Completion tokens: " << tok.value("completion", 0) << "\n";
}

static void PrintHistory(TinyAgent& agent) {
  const auto msgs = agent.GetAllMessages();
  if (msgs.empty()) { std::cout << "(no history)\n"; return; }

  for (const auto& m : msgs) {
    const std::string role = m.value("role", "?");
    if (role == "system") continue;  // skip verbose system prompt

    std::string label;
    if      (role == "user")      label = color::wrap(color::bold,   "You");
    else if (role == "assistant") label = color::wrap(color::cyan,   "Bot");
    else if (role == "tool")      label = color::wrap(color::yellow, "Tool");
    else                          label = role;

    std::cout << "[" << label << "] ";

    if (m.contains("content") && m["content"].is_string())
      std::cout << m["content"].get<std::string>();
    else if (m.contains("tool_calls"))
      std::cout << "<tool calls>";

    std::cout << "\n";
  }
}

// Run one user turn and print streaming events to stdout.
static void RunTurn(TinyAgent& agent, const std::string& message) {
  bool started_text = false;

  agent.ChatStream(message, [&](const nlohmann::json& event) {
    if (g_interrupted) return;

    const std::string type = event.value("type", "");

    if (type == "text_delta") {
      if (!started_text) {
        std::cout << color::wrap(color::cyan, "Bot: ");
        started_text = true;
      }
      std::cout << event.value("content", "") << std::flush;

    } else if (type == "tool_call_start") {
      if (started_text) { std::cout << "\n"; started_text = false; }
      std::cout << color::wrap(color::yellow, "[tool] ")
                << event.value("name", "?")
                << " " << color::wrap(color::dim, event.value("arguments", "{}"))
                << "\n" << std::flush;

    } else if (type == "tool_call_end") {
      std::cout << color::wrap(color::yellow, "[done] ")
                << color::wrap(color::dim, event.value("result_summary", ""))
                << "\n" << std::flush;

    } else if (type == "error") {
      if (started_text) { std::cout << "\n"; started_text = false; }
      std::cerr << color::wrap(color::red, "Error: ") << event.value("content", "") << "\n";

    } else if (type == "token_usage") {
      // Silently tracked; uncomment to display:
      // std::cerr << "[tokens] prompt=" << event.value("prompt_tokens",0)
      //           << " completion=" << event.value("completion_tokens",0) << "\n";
    }
  });

  if (started_text) std::cout << "\n";
}

int main(int argc, char* argv[]) {
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Disable colours when output is not a terminal.
  if (!isatty(fileno(stdout))) color::enabled = false;

  std::signal(SIGINT, SigintHandler);

  // ---- Load config -------------------------------------------------------
  LlmConfig llm_cfg;

  const std::string config_path = "config.yaml";
  if (fs::exists(config_path)) {
    try {
      YAML::Node cfg = YAML::LoadFile(config_path);
      llm_cfg = ResolveLlmConfig(cfg);
    } catch (const std::exception& e) {
      std::cerr << "Failed to parse config.yaml: " << e.what() << "\n";
      return 1;
    }
  } else {
    llm_cfg = ResolveLlmConfig(YAML::Node{});
  }

  // ---- Workspace ---------------------------------------------------------
  const std::string workspace_path = "./workspace";
  fs::create_directories(fs::path(workspace_path) / "outputs");

  // ---- Agent -------------------------------------------------------------
  TinyAgent* agent = nullptr;
  try {
    agent = new TinyAgent(workspace_path, api_key, base_url, model);
  } catch (const std::exception& e) {
    std::cerr << color::wrap(color::red, "Error: ") << e.what() << "\n";
    curl_global_cleanup();
    return 1;
  }

  // ---- One-shot mode: message passed as CLI argument ---------------------
  if (argc >= 2) {
    std::string msg;
    for (int i = 1; i < argc; ++i) {
      if (i > 1) msg += ' ';
      msg += argv[i];
    }
    RunTurn(*agent, msg);
    delete agent;
    curl_global_cleanup();
    return 0;
  }

  // ---- Interactive REPL --------------------------------------------------
  const bool is_tty = isatty(fileno(stdin));

  if (is_tty) {
    std::cout << color::wrap(color::bold, "Tiny Agent CLI")
              << " — provider: " << color::wrap(color::green, llm_cfg.provider)
              << "  model: " << color::wrap(color::cyan, llm_cfg.model)
              << "  workspace: " << workspace_path << "\n"
              << "Type " << color::wrap(color::bold, "/help") << " for commands, "
              << color::wrap(color::bold, "Ctrl-D") << " or "
              << color::wrap(color::bold, "/quit") << " to exit.\n\n";
  }

  std::string line;
  while (true) {
    if (is_tty) std::cout << color::wrap(color::bold, "You: ") << std::flush;

    g_interrupted = false;
    if (!std::getline(std::cin, line)) break;  // EOF / Ctrl-D

    // Trim leading/trailing whitespace.
    auto ltrim = line.find_first_not_of(" \t\r\n");
    if (ltrim == std::string::npos) continue;
    line = line.substr(ltrim);
    auto rtrim = line.find_last_not_of(" \t\r\n");
    if (rtrim != std::string::npos) line = line.substr(0, rtrim + 1);

    if (line.empty()) continue;

    // Slash commands.
    if (line == "/quit" || line == "/exit" || line == "/q") break;
    if (line == "/help"    || line == "/?")    { PrintHelp();          continue; }
    if (line == "/status")                     { PrintStatus(*agent);  continue; }
    if (line == "/memory")                     { PrintMemory(*agent);  continue; }
    if (line == "/tokens")                     { PrintTokens(*agent);  continue; }
    if (line == "/history")                    { PrintHistory(*agent); continue; }
    if (line == "/clear") {
      agent->ClearMemory();
      std::cout << color::wrap(color::dim, "(conversation cleared)\n");
      continue;
    }

    RunTurn(*agent, line);
  }

  if (is_tty) std::cout << "\nGoodbye.\n";

  delete agent;
  curl_global_cleanup();
  return 0;
}
