cmake_minimum_required(VERSION 3.16)
project(LOBE VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compiler flags ────────────────────────────────────────────────────────────
add_compile_options(-O3 -march=native -Wall -Wextra -g)

# ── Shared core library ───────────────────────────────────────────────────────
add_library(lobe_core src/order_book.cpp)
target_include_directories(lobe_core PUBLIC include)

# ── 1. Interactive CLI  (./lobe) ──────────────────────────────────────────────
add_executable(lobe src/main.cpp)
target_link_libraries(lobe PRIVATE lobe_core)

# ── 2. JSON server mode  (./lobe_server) ──────────────────────────────────────
#    Reads JSON orders from stdin, writes JSON events to stdout.
#    Spawned by the Node.js feed server.
add_executable(lobe_server src/server_main.cpp)
target_link_libraries(lobe_server PRIVATE lobe_core)

# ── 3. Benchmark  (./lobe_bench) ──────────────────────────────────────────────
add_executable(lobe_bench benchmark/bench.cpp)
target_link_libraries(lobe_bench PRIVATE lobe_core)
