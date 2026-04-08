# tiny_agent_cli

A minimal AI agent that runs entirely in your terminal. No HTTP server, no web UI — just a pure CLI REPL backed by OpenAI-compatible APIs (OpenAI, OpenRouter, Ollama, LM Studio).

## Features

- Streaming responses printed to stdout as they arrive
- Tool use: read/write/edit files and run shell commands
- MCP tool support via stdio servers (tools are auto-registered at startup)
- Persistent conversation history and long-term memory (markdown)
- Skill system: drop a `SKILL.md` into `workspace/skills/<name>/` to extend the agent
- One-shot mode for scripting
- ANSI colour output (auto-disabled when piped)

## Requirements

- C++17 compiler (GCC ≥ 10 or Clang ≥ 12)
- CMake ≥ 3.20
- libcurl
- yaml-cpp
- nlohmann/json (system header-only package)

On Gentoo/Arch/Debian-based systems:

```bash
# Gentoo
emerge net-misc/curl dev-libs/yaml-cpp dev-cpp/nlohmann_json

# Arch
pacman -S curl yaml-cpp nlohmann-json

# Debian/Ubuntu
apt install libcurl4-openssl-dev libyaml-cpp-dev nlohmann-json3-dev
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The binary is at `build/tiny_agent_cli`.

## Configuration

Copy the example config and fill in your API key:

```bash
cp config.yaml.example config.yaml
```

```yaml
llm:
  provider: "openai"   # openai | openrouter | ollama | lmstudio
  api_key: "sk-..."    # optional for local providers
  model: "gpt-4o-mini"
  # base_url: "https://api.openai.com/v1" # optional; defaults by provider

mcp:
  servers:
    - name: "filesystem"
      command: "npx"
      args: ["-y", "@modelcontextprotocol/server-filesystem", "./workspace"]
      # cwd: "."
      # enabled: true
      # env:
      #   SOME_API_KEY: "..."
```

Alternatively, set `OPENAI_API_KEY` (default/OpenAI-compatible) or `OPENROUTER_API_KEY` (OpenRouter) — no config file needed.

Config loading and resolution are centralized in `src/llmconfig.hpp/cpp` (`LoadLlmConfig("config.yaml")`), including provider defaults, model aliases, and env var fallback.

Provider defaults:

- `openai` -> `https://api.openai.com/v1`
- `openrouter` -> `https://openrouter.ai/api/v1`
- `ollama` -> set `base_url: "http://localhost:11434/v1"` in config
- `lmstudio` -> set `base_url: "http://localhost:1234/v1"` in config

Any OpenAI-compatible endpoint still works by overriding `base_url`. `provider` mainly controls default URL and API key env var selection (`openrouter` checks `OPENROUTER_API_KEY`; all others fall back to `OPENAI_API_KEY`).

MCP notes:

- Only stdio MCP servers are supported.
- MCP tools are discovered at startup via `tools/list`.
- Each discovered tool is exposed as an OpenAI tool with local name format:
  - `mcp_<server_name>__<tool_name>` (sanitized to `[A-Za-z0-9_]`).
- If an MCP server fails to start or initialize, it is skipped and built-in tools still work.

Examples:

```yaml
# OpenAI
llm:
  provider: "openai"
  api_key: "sk-..."
  model: "gpt-4o-mini"

# OpenRouter
llm:
  provider: "openrouter"
  api_key: "sk-or-v1-..."
  model: "openai/gpt-4o-mini"
  # base_url defaults to https://openrouter.ai/api/v1

# Ollama
llm:
  provider: "ollama"
  api_key: ""              # not required for local Ollama
  model: "llama3.1:8b"
  base_url: "http://localhost:11434/v1"

# LM Studio
llm:
  provider: "lmstudio"
  api_key: "lm-studio"     # placeholder; some clients require non-empty
  model: "local-model"
  base_url: "http://localhost:1234/v1"
```

OpenRouter Free Models Router support:

- If `llm.provider: "openrouter"` and `llm.model: "free-models-router"`, the CLI maps it to `openrouter/free`.
- This follows OpenRouter docs: https://openrouter.ai/docs/guides/routing/routers/free-models-router

## Usage

### Interactive REPL

```bash
./build/tiny_agent_cli
```

```
Tiny Agent CLI — provider: openai  model: gpt-4o-mini  workspace: ./workspace
Type /help for commands, Ctrl-D or /quit to exit.

You: summarise the file ./notes.txt
Bot: ...
You: /clear
(conversation cleared)
You: /quit
```

### One-shot mode

Pass a message as command-line arguments:

```bash
./build/tiny_agent_cli "list files in the current directory"
```

Stdout is plain text when not a TTY, making it pipe-friendly:

```bash
./build/tiny_agent_cli "write a haiku about C++" | tee haiku.txt
```

## REPL commands

| Command    | Description                        |
|------------|------------------------------------|
| `/help`    | Show command list                  |
| `/clear`   | Clear conversation history         |
| `/status`  | Show loaded skills and tools       |
| `/mcp`     | Show MCP server and tool status    |
| `/memory`  | Show long-term memory              |
| `/tokens`  | Show cumulative token usage        |
| `/history` | Print conversation history         |
| `/quit`    | Exit                               |

## Workspace layout

```
workspace/
  memory/
    default_history.json   conversation history
    default_tokens.json    token usage counters
    MEMORY.md              long-term memory (written by the agent)
  outputs/                 all agent-generated files go here
  skills/
    <skill-name>/
      SKILL.md             skill definition with optional YAML frontmatter
```

### Skill frontmatter

```markdown
---
description: "Short description shown in /status"
active: true
always_load: false   # true = injected into every system prompt
---

Skill content here...
```

## Project structure

```
src/
  main.cpp                 CLI entry point (REPL + one-shot)
  llmconfig.hpp/cpp        LLM config file loading + provider/env resolution
  tiny_agent.hpp/cpp       Top-level agent facade
  agent_loop.hpp/cpp       Iterative model/tool loop
  openai_stream_client.hpp/cpp  Streaming HTTP client (libcurl)
  context_builder.hpp/cpp  System prompt + message assembly
  memory_store.hpp/cpp     History and token persistence
  skills_loader.hpp/cpp    Skill discovery and parsing
  tools.hpp/cpp            Built-in tools + MCP dynamic tool registration
  mcp_client.hpp/cpp       Stdio MCP JSON-RPC client (initialize/list/call)
CMakeLists.txt
```

Headers are co-located with their source files under `src/`.
