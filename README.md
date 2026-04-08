# tiny_agent_cli

A minimal AI agent that runs entirely in your terminal. No HTTP server, no web UI — just a pure CLI REPL backed by any OpenAI-compatible API.

## Features

- Streaming responses printed to stdout as they arrive
- Tool use: read/write/edit files and run shell commands
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
  api_key: "sk-..."
  model: "gpt-4o-mini"
  base_url: "https://api.openai.com/v1"
```

Alternatively, set the `OPENAI_API_KEY` environment variable — no config file needed.

Any OpenAI-compatible endpoint works (Ollama, vLLM, LiteLLM, etc.) by changing `base_url`.

## Usage

### Interactive REPL

```bash
./build/tiny_agent_cli
```

```
Tiny Agent CLI — model: gpt-4o-mini  workspace: ./workspace
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
  tiny_agent.hpp/cpp       Top-level agent facade
  agent_loop.hpp/cpp       Iterative model/tool loop
  openai_stream_client.hpp/cpp  Streaming HTTP client (libcurl)
  context_builder.hpp/cpp  System prompt + message assembly
  memory_store.hpp/cpp     History and token persistence
  skills_loader.hpp/cpp    Skill discovery and parsing
  tools.hpp/cpp            Built-in tools (read_file, write_file, edit_file, exec)
CMakeLists.txt
```

Headers are co-located with their source files under `src/`.
