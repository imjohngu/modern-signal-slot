#pragma once

#include "./core/signal.hpp"
#include "./signal-slot/core/task_queue.hpp"

// Macro for signal declaration
#define SIGNAL(name, ...) \
    sigslot::signal<__VA_ARGS__> name;

// Macro for slot declaration
#define SLOT(name) &name

// Macro for connecting signal and slot
#define CONNECT_IMPL4(sender, signal, receiver, slot, ...) \
    (sender)->signal.connect(receiver, slot, ##__VA_ARGS__)

#define CONNECT_IMPL3(sender, signal, slot, ...) \
    (sender)->signal.connect(slot, ##__VA_ARGS__)

#define GET_CONNECT_MACRO(_1, _2, _3, _4, _5, _6, NAME, ...) NAME

// Modify CONNECT macro to handle member access correctly
#define CONNECT(...) \
    GET_CONNECT_MACRO(__VA_ARGS__, \
                     CONNECT_IMPL4, CONNECT_IMPL4, \
                     CONNECT_IMPL3, CONNECT_IMPL3, \
                     CONNECT_IMPL3)(__VA_ARGS__)

// Macro for disconnecting signal and slot
#define DISCONNECT(sender, signal, receiver, slot) \
    (sender)->signal.disconnect(receiver, slot)

// Macro for emitting signal
#define EMIT(signal, ...) signal(__VA_ARGS__)
 