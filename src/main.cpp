#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "llmconfig.hpp"
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
      << "Ctrl-C during a response cancels the current request.\n"
      << "Ctrl-D at the prompt or /quit to exit.\n";
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
  bool cancelled = false;

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
      if (event.value("content", "") == "API request cancelled")
        cancelled = true;

    } else if (type == "token_usage") {
      // Silently tracked; uncomment to display:
      // std::cerr << "[tokens] prompt=" << event.value("prompt_tokens",0)
      //           << " completion=" << event.value("completion_tokens",0) << "\n";
    }
  }, []() { return g_interrupted; });

  if (started_text) std::cout << "\n";
  if (g_interrupted || cancelled)
    std::cout << color::wrap(color::dim, "(request cancelled)\n");
}

int main(int argc, char* argv[]) {
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Disable colours when output is not a terminal.
  if (!isatty(fileno(stdout))) color::enabled = false;

  std::signal(SIGINT, SigintHandler);

  // ---- Load config -------------------------------------------------------
  LlmConfig llm_cfg;
  try {
    llm_cfg = LoadLlmConfig("config.yaml");
  } catch (const std::exception& e) {
    std::cerr << "Failed to parse config.yaml: " << e.what() << "\n";
    return 1;
  }

  // ---- Workspace ---------------------------------------------------------
  const std::string workspace_path = "./workspace";
  fs::create_directories(fs::path(workspace_path) / "outputs");

  // ---- Agent -------------------------------------------------------------
  TinyAgent* agent = nullptr;
  try {
    agent = new TinyAgent(workspace_path, llm_cfg.api_key, llm_cfg.base_url, llm_cfg.model);
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
              << color::wrap(color::bold, "Ctrl-D at prompt") << " or "
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
