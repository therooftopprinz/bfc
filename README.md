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

## Reactive queue wakeups

Both `reactive_event_queue` and `reactive_circular_lossy_queue` can wake a
reactor automatically after each enqueue:

```cpp
auto reactor = std::make_shared<bfc::cv_reactor<>>();
bfc::reactive_circular_lossy_queue<int> queue;
queue.set_capacity(64);
queue.set_reactor(reactor);
reactor->add_read_rdy(queue, [&queue] {
    while (auto item = queue.pop()) {
        // Process *item on the reactor thread.
    }
});
queue.push(42); // Wakes reactor; run() dispatches the registered handler.
```

`set_reactor(std::shared_ptr<T>)` needs only `T::wake_up()`; the queue's type does
not depend on `T`. The binding retains shared ownership of the reactor.
`set_reactor(nullptr)` or an empty shared pointer clears the binding. Binding,
replacement, and clearing are synchronized with pushes; a push already in
flight may still wake the previous reactor safely. Notification runs after the
queue mutex is released. A capacity-zero lossy queue does not enqueue or wake.

Binding and read callback registration are separate. Register one consumer
reactor per queue and bind that same reactor. Registration does not notify for
existing items: explicitly wake after registering a queue that already contains
data. Unbound queues retain the explicit `wake_up()` behavior. Handlers must
drain their queues because reactor wakeups can coalesce.

Clearing the binding does not unregister the read callback or wait for an
in-flight callback. Before destroying a registered queue or callback state,
remove the read registration and synchronize with the reactor thread (or stop
and join it). Keep the queue alive until dispatch has finished. Avoid having
a reactor-owned handler retain shared ownership of its bound queue, which
would create an ownership cycle through the queue's reactor binding.

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
