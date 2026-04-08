cmake_minimum_required(VERSION 3.20)
project(tiny_agent_cli VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(tiny_agent_cli
  src/main.cpp
  src/memory_store.cpp
  src/skills_loader.cpp
  src/context_builder.cpp
  src/tools.cpp
  src/llmconfig.cpp
  src/mcp_client.cpp
  src/openai_stream_client.cpp
  src/agent_loop.cpp
  src/tiny_agent.cpp
)

target_include_directories(tiny_agent_cli PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(tiny_agent_cli PRIVATE
  CURL::libcurl
  yaml-cpp
)

# nlohmann/json is header-only and available system-wide.
# Install via your distro package if missing (e.g. nlohmann-json or nlohmann_json).
