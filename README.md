# BFC

BFC is a header-only C++17 utility library that provides reusable building blocks
for event-driven and systems programming.

## What's included

The library is exposed through headers in `src/bfc` and currently includes:

- buffer and sized-buffer utilities
- lightweight function wrappers
- event queues and timers
- command/configuration helpers
- thread pool and memory pool primitives
- metrics/monitoring helpers
- socket abstractions
- poll/epoll/condition-variable reactor implementations

## Requirements

- CMake 3.15+
- C++17 compiler
- POSIX environment for reactor/socket features

## Reactor threading contract

- `poll_reactor` and `epoll_reactor` are single-threaded by design.
- FD registration and interest management APIs (`add_*`, `rem_*`, `req_*`) must be called from the same thread that runs `run()`.
- Cross-thread signaling is supported via `wake_up(...)` and `stop()`.

## Build tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/bfc_tests
```

The test binary includes unit tests from `tests/ut` and end-to-end tests from
`tests/e2e`.

## Install

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /usr/local
```

This installs:

- headers to `include/bfc`
- CMake package config files to `lib/cmake/bfc`

## Use from CMake

After installation:

```cmake
find_package(bfc CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE bfc::bfc)
```
