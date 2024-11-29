# Signal-Slot Library

[English](README.md) | [中文](readme_i18n/README_zh.md)

## Introduction

This is a modern C++ implementation of the signal-slot pattern, providing a thread-safe event handling mechanism. It supports both single-threaded and multi-threaded environments, with features like connection management, slot lifetime tracking, and execution ordering.

## Features

- Thread-safe signal emission and slot management
- Support for different connection types:
  - Direct connection
  - Queued connection 
  - Blocking queued connection
  - Single-shot connection
  - Unique connection
- Automatic connection management via RAII
- Support for member functions, free functions, lambdas
- Slot grouping and execution ordering
- Weak pointer tracking for automatic disconnection
- Observer pattern support
- Cross-thread signal-slot communication

## Usage

### Basic Usage

```cpp
// Example data structures
struct DeviceInfo {
    std::string deviceId;
    std::string deviceName;
};

struct VideoFrame {
    int width;
    int height;
    std::vector<uint8_t> data;
};

// Signal emitter class
class DeviceController {
public:
    // Signals declaration
    SIGNAL(started);                                           // No parameter signal
    SIGNAL(devicePlugged, const std::shared_ptr<DeviceInfo>&); // Single parameter signal
    SIGNAL(progress, int, int, const std::string&);           // Multiple parameters signal
    SIGNAL(frameReceived, const VideoFrame&);                 // Custom type signal
};

// Signal receiver class
class UiController {
public:
    // No parameter slot
    void onStarted() {
        std::cout << "UiController: Device started" << std::endl;
    }

    // Single parameter slot
    void onDevicePlugged(const std::shared_ptr<DeviceInfo>& info) {
        std::cout << "UiController: Device plugged - " << info->deviceName << std::endl;
    }

    // Multiple parameters slot
    void onProgress(int current, int total, const std::string& message) {
        std::cout << "UiController: Progress " << current << "/" << total 
                 << " - " << message << std::endl;
    }
};

// Usage example
int main() {
    auto dc = std::make_shared<DeviceController>();
    auto ui = std::make_shared<UiController>();

    // Connect member function slots
    CONNECT(dc, started, ui.get(), SLOT(UiController::onStarted),
            sigslot::connection_type::auto_connection, TQ("worker"));

    CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePlugged),
            sigslot::connection_type::direct_connection, TQ("worker"));

    // Connect lambda slots
    CONNECT(dc, frameReceived, [](const VideoFrame& frame) {
        std::cout << "Lambda: Received frame " << frame.width << "x" << frame.height << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));
}
```

### Connection Types

```cpp
// Auto connection (system decides based on thread context)
CONNECT(dc, started, ui.get(), SLOT(UiController::onStarted),
        sigslot::connection_type::auto_connection, TQ("worker"));

// Direct connection (synchronous execution)
CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePlugged),
        sigslot::connection_type::direct_connection, TQ("worker"));

// Queued connection (asynchronous execution)
CONNECT(dc, progress, ui.get(), SLOT(UiController::onProgress),
        sigslot::connection_type::queued_connection, TQ("worker"));

// Blocking queued connection (asynchronous but waits for completion)
CONNECT(dc, error, ui.get(), SLOT(UiController::onError),
        sigslot::connection_type::blocking_queued_connection, TQ("worker"));

// Single-shot connection (disconnects after first execution)
CONNECT(dc, error, errorHandler,
        sigslot::connection_type::queued_connection | sigslot::connection_type::singleshot_connection,
        TQ("worker"));

// Unique connection (prevents duplicate connections)
CONNECT(dc, error, errorHandler,
        sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
        TQ("worker"));
```

### Connection Management

```cpp
// RAII connection management
auto conn = sig.connect(slot);
conn.disconnect(); // Manual disconnection

// Scoped connection
{
    sigslot::scoped_connection conn = sig.connect(slot);
    // Auto-disconnects when scope ends
}
```

## Build Requirements

- C++14 or higher
- CMake 3.10 or higher
- Threading support in standard library

## Implementation Details

The library consists of several key components:

- `signal.hpp`: Core signal-slot implementation
- `signal_slot_api.hpp`: User-friendly API macros
- `task_queue.hpp`: Task queue interface for asynchronous execution
- `task_queue_manager.hpp`: Task queue management

## Notes

1. Pay attention to thread safety when using in multi-threaded environments
2. Ensure event loop is running when using queued connections
3. Be mindful of object lifecycles when using weak pointer tracking
4. Choose appropriate connection types to avoid unnecessary overhead

## Examples

For more usage examples, please refer to `main.cpp` and `test_signal_slot.cpp`.