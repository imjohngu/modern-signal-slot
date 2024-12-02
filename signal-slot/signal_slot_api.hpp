#pragma once

#include "./core/signal.hpp"
#include "./signal-slot/core/task_queue.hpp"

namespace sigslot {

// Signal class template
template<typename... Args>
class Signal {
private:
    signal<Args...> sig;

public:
    Signal() = default;
    
    // Expose internal signal's operations
    template<typename... EmitArgs>
    void operator()(EmitArgs&&... args) {
        sig(std::forward<EmitArgs>(args)...);
    }

    void disconnect_all() { sig.disconnect_all(); }
    
    // Make internal signal accessible to free functions
    signal<Args...>& get_internal() { return sig; }
    const signal<Args...>& get_internal() const { return sig; }
};

// Global functions for connection management
// For member functions
template<typename Sender, typename Signal, typename Receiver, typename Slot>
connection connect(Sender* sender, Signal& signal, Receiver* receiver, Slot slot,
                  uint32_t type = connection_type::direct_connection,
                  core::TaskQueue* queue = nullptr) {
    return signal.get_internal().connect(receiver, slot, type, queue);
}

// For lambdas and free functions
template<typename Sender, typename Signal, typename Callable>
connection connect(Sender* sender, Signal& signal, Callable&& callable,
                  uint32_t type = connection_type::direct_connection,
                  core::TaskQueue* queue = nullptr) {
    return signal.get_internal().connect(std::forward<Callable>(callable), type, queue);
}

// Global function for signal emission
template<typename Signal, typename... Args>
void emit(Signal& signal, Args&&... args) {
    signal(std::forward<Args>(args)...);
}

// Global functions for disconnection
template<typename Sender, typename Signal, typename Receiver, typename Slot>
void disconnect(Sender* sender, Signal& signal, Receiver* receiver, Slot slot) {
    signal.get_internal().disconnect(receiver, slot);
}

template<typename Signal>
void disconnect_all(Signal& signal) {
    signal.disconnect_all();
}

} // namespace sigslot
 