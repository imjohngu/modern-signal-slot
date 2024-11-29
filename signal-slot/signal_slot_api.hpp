#pragma once

#include "./core/signal.hpp"
#include "./signal-slot/core/task_queue.hpp"

// Macro for signal declaration
#define SIGNAL(name, ...) \
    sigslot::signal<__VA_ARGS__> name;

// Macro for slot declaration
#define SLOT(name) &name

// For member functions with type and queue
#define CONNECT_IMPL6(sender, signal, receiver, slot, type, queue) \
    (sender)->signal.connect(receiver, slot, type, queue)

// For member functions without type and queue
#define CONNECT_IMPL4(sender, signal, receiver, slot) \
    (sender)->signal.connect(receiver, slot, sigslot::connection_type::direct_connection, nullptr)

// For lambdas/global functions with type and queue
#define CONNECT_IMPL5(sender, signal, slot, type, queue) \
    (sender)->signal.connect(slot, type, queue)

// For lambdas/global functions without type and queue
#define CONNECT_IMPL3(sender, signal, slot) \
    (sender)->signal.connect(slot, sigslot::connection_type::direct_connection, nullptr)

// Helper macro to count arguments
#define COUNT_ARGS(...) COUNT_ARGS_HELPER(__VA_ARGS__, 6, 5, 4, 3, 2, 1)
#define COUNT_ARGS_HELPER(_1, _2, _3, _4, _5, _6, N, ...) N

// Helper macro to detect if argument is a receiver
#define IS_MEMBER_FUNC(...) \
    IS_MEMBER_FUNC_HELPER(__VA_ARGS__, 0, 0, 0, 0, 0)
#define IS_MEMBER_FUNC_HELPER(_1, _2, _3, _4, _5, _6, N, ...) N

// Choose the appropriate implementation based on argument count and type
#define CONNECT(...) \
    CONNECT_CHOOSER(COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define CONNECT_CHOOSER(N) CONNECT_CHOOSER_(N)
#define CONNECT_CHOOSER_(N) CONNECT_IMPL ## N

// Macro for disconnecting signal and slot
#define DISCONNECT(sender, signal, receiver, slot) \
    (sender)->signal.disconnect(receiver, slot)

// Macro for emitting signal
#define EMIT(signal, ...) signal(__VA_ARGS__)
 