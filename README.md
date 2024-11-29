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

The library supports several connection types through the `connection_type` enum:

```cpp
enum connection_type {
    auto_connection = 0,            // System automatically chooses direct or queued based on thread context
    direct_connection = 1,          // Synchronous execution in the emitting thread
    queued_connection = 2,          // Asynchronous execution in the target thread
    blocking_queued_connection = 3, // Asynchronous execution but blocks until completion
    unique_connection = 0x80,       // Ensures only one identical connection exists (can be combined with other types)
    singleshot_connection = 0x100   // Connection automatically disconnects after first execution (can be combined with other types)
};
```

- `auto_connection`: The system automatically chooses between direct and queued connection:
  - If no task queue is specified (nullptr or not specified):
    * Always uses direct connection
    * Executes in the emitting thread
  - If a task queue is specified:
    * Uses direct connection if emitting from the queue's thread
    * Uses queued connection if emitting from other threads
    * Executes in the task queue's thread when using queued connection

- `direct_connection`: The slot is executed synchronously in the thread that emits the signal. This is the fastest option but may block the emitting thread.

- `queued_connection`: The slot execution is queued and will be executed asynchronously in the target thread's event loop. This is safe for cross-thread signal-slot communication.

- `blocking_queued_connection`: Similar to queued connection, but the signal emission blocks until the slot execution completes. Useful when you need the results of the slot execution.

- `unique_connection`: Can be combined with other connection types using the OR operator (|). Ensures that the same slot is not connected multiple times to the same signal.

- `singleshot_connection`: Can be combined with other connection types using the OR operator (|). The connection will automatically disconnect after the slot is executed once.

Example combinations:
```cpp
// Unique queued connection
connection_type::queued_connection | connection_type::unique_connection

// Single-shot blocking queued connection
connection_type::blocking_queued_connection | connection_type::singleshot_connection
```

### Connection Management

```cpp
// Method 0: Default connection (without specifying type and queue)
// Member function
CONNECT(sender, signal, receiver.get(), SLOT(Receiver::slot));

// Lambda
CONNECT(sender, signal, [](int value) {
    std::cout << "Received value: " << value << std::endl;
});

// Global function
CONNECT(sender, signal, globalHandler);

// Method 1: Using connection object
auto conn = CONNECT(sender, signal, receiver, SLOT(Receiver::slot),
                   connection_type::auto_connection, TQ("worker"));
conn.disconnect();  // Manual disconnection

// Method 2: Using DISCONNECT macro
DISCONNECT(sender, signal, receiver, SLOT(Receiver::slot));

// Method 3: Scoped connection (RAII)
{
    sigslot::scoped_connection conn = CONNECT(sender, signal, receiver, slot);
    // Connection is valid within this scope
} // Automatically disconnects when leaving scope

// Method 4: Disconnect all slots from a signal
sender->signal.disconnect_all();
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

### Complete Usage Guide

#### Required Headers
```cpp
#include <iostream>
#include <memory>
#include <thread>
#include "./signal-slot/signal_slot_api.hpp"
#include "./signal-slot/core/task_queue_manager.hpp"
```

#### 1. Initialize Task Queue Manager
```cpp
int main() {
    // Create a task queue named "worker"
    TQMgr->create({"worker"});
}
```

#### 2. Define Signal Emitter
```cpp
class DeviceController {
public:
    // Signal without parameters
    SIGNAL(started);
    
    // Signal with single parameter
    SIGNAL(devicePlugged, const std::shared_ptr<DeviceInfo>&);
    
    // Signal with multiple parameters
    SIGNAL(progress, int, int, const std::string&);
    
    // Signal with basic type parameter
    SIGNAL(error, const std::string&);
    
    // Signal with custom type parameter
    SIGNAL(frameReceived, const VideoFrame&);
};
```

#### 3. Define Signal Receiver
```cpp
class UiController {
public:
    // Slot without parameters
    void onStarted() {
        std::cout << "Device started" << std::endl;
    }

    // Slot with single parameter
    void onDevicePlugged(const std::shared_ptr<DeviceInfo>& info) {
        std::cout << "Device plugged - " << info->deviceName << std::endl;
    }

    // Slot with multiple parameters
    void onProgress(int current, int total, const std::string& message) {
        std::cout << "Progress " << current << "/" << total 
                 << " - " << message << std::endl;
    }
};
```

#### 4. Connection Examples

1. Basic Connection (without task queue)
```cpp
CONNECT(dc, started, ui.get(), SLOT(UiController::onStarted),
        sigslot::connection_type::auto_connection);
```

2. Direct Connection with Task Queue
```cpp
CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePlugged),
        sigslot::connection_type::direct_connection, TQ("worker"));
```

3. Queued Connection
```cpp
CONNECT(dc, progress, ui.get(), SLOT(UiController::onProgress),
        sigslot::connection_type::queued_connection, TQ("worker"));
```

4. Lambda Connection
```cpp
CONNECT(dc, frameReceived, [](const VideoFrame& frame) {
    std::cout << "Received frame " << frame.width << "x" << frame.height << std::endl;
}, sigslot::connection_type::queued_connection, TQ("worker"));
```

5. Single-shot Connection
```cpp
CONNECT(dc, error, [](const std::string& error) {
    std::cout << "One-shot error handler - " << error << std::endl;
}, sigslot::connection_type::queued_connection | sigslot::connection_type::singleshot_connection, 
TQ("worker"));
```

6. Unique Connection
```cpp
CONNECT(dc, error, [](const std::string& error) {
    std::cout << "Unique error handler - " << error << std::endl;
}, sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
TQ("worker"));
```

#### 5. Signal Emission
```cpp
// Emit signal without parameters
EMIT(started);

// Emit signal with single parameter
auto deviceInfo = std::make_shared<DeviceInfo>();
deviceInfo->deviceName = "microphone";
EMIT(devicePlugged, deviceInfo);

// Emit signal with multiple parameters
EMIT(progress, 1, 3, "Processing...");

// Emit signal with custom type
VideoFrame frame{640, 480, std::vector<uint8_t>(640*480, 0)};
EMIT(frameReceived, frame);
```

#### 6. Connection Management Examples

1. Using Connection Object
```cpp
auto conn = CONNECT(dc, progress, [](int current, int total, const std::string& message) {
    std::cout << "Progress " << current << "/" << total << std::endl;
}, sigslot::connection_type::queued_connection, TQ("worker"));

conn.disconnect();  // Manual disconnection
```

2. Using Scoped Connection
```cpp
{
    sigslot::scoped_connection conn = CONNECT(dc, error, [](const std::string& error) {
        std::cout << "Scoped error handler: " << error << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));
    
    // Connection is valid only within this scope
} // Automatically disconnects here
```

3. Using DISCONNECT Macro
```cpp
DISCONNECT(dc, started, nullptr, nullptr);  // Disconnect all slots from 'started' signal
```

## More Examples

For more detailed examples and usage scenarios, you can refer to:

- `examples.cpp`: Demonstrates comprehensive usage including:
  - Different connection types (direct, queued, auto, blocking)
  - Various signal-slot patterns (member functions, lambdas, global functions)
  - Connection management methods
  - Thread-safe signal emission
  - Task queue integration
  - Thread ID tracking for debugging

- `test_signal_slot.cpp`: Contains unit tests that demonstrate:
  - Basic signal-slot functionality
  - Connection type behaviors:
    * Auto connection: Tests behavior with/without task queue
    * Direct connection: Verifies execution in emitting thread
    * Queued connection: Tests asynchronous execution
    * Blocking queued connection: Verifies blocking behavior and execution time
  - Special connection types:
    * Single-shot connection: Tests one-time execution and auto-disconnection
    * Unique connection: Verifies prevention of duplicate connections
  - Combined connection types:
    * Tests unique + single-shot + queued combinations
    * Verifies correct behavior when flags are combined
  - Connection management:
    * Manual disconnection
    * Scoped connection lifetime
    * Connection blocking/unblocking
    * Disconnect all slots
  - Thread safety aspects:
    * Cross-thread signal emission
    * Thread ID verification
    * Task queue integration

These files provide practical examples and test cases that can help you understand how to use the library effectively in your own projects.