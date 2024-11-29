# 信号槽库

[English](../README.md) | [中文](README_zh.md)

## 简介

这是一个现代C++实现的信号槽库，提供了线程安全的事件处理机制。它同时支持单线程和多线程环境，具有连接管理、槽生命周期跟踪和执行顺序控制等特性。

## 特性

- 线程安全的信号发射和槽管理
- 支持多种连接类型：
  - 直接连接
  - 队列连接
  - 阻塞队列连接
  - 单次连接
  - 唯一连接
- 通过RAII进行自动连接管理
- 支持成员函数、自由函数、Lambda表达式
- 槽分组和执行顺序控制
- 弱指针跟踪实现自动断开连接
- 支持观察者模式
- 跨线程信号槽通信

## 使用方法

### 基本用法

```cpp
// 示例数据结构
struct DeviceInfo {
    std::string deviceId;
    std::string deviceName;
};

struct VideoFrame {
    int width;
    int height;
    std::vector<uint8_t> data;
};

// 信号发送者类
class DeviceController {
public:
    // 信号声明
    SIGNAL(started);                                           // 无参数信号
    SIGNAL(devicePlugged, const std::shared_ptr<DeviceInfo>&); // 单参数信号
    SIGNAL(progress, int, int, const std::string&);           // 多参数信号
    SIGNAL(frameReceived, const VideoFrame&);                 // 自定义类型信号
};

// 信号接收者类
class UiController {
public:
    // 无参数槽函数
    void onStarted() {
        std::cout << "UiController: 设备已启动" << std::endl;
    }

    // 单参数槽函数
    void onDevicePlugged(const std::shared_ptr<DeviceInfo>& info) {
        std::cout << "UiController: 设备已插入 - " << info->deviceName << std::endl;
    }

    // 多参数槽函数
    void onProgress(int current, int total, const std::string& message) {
        std::cout << "UiController: 进度 " << current << "/" << total 
                 << " - " << message << std::endl;
    }
};

// 使用示例
int main() {
    auto dc = std::make_shared<DeviceController>();
    auto ui = std::make_shared<UiController>();

    // 连接成员函数槽
    CONNECT(dc, started, ui.get(), SLOT(UiController::onStarted),
            sigslot::connection_type::auto_connection, TQ("worker"));

    CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePlugged),
            sigslot::connection_type::direct_connection, TQ("worker"));

    // 连接Lambda槽
    CONNECT(dc, frameReceived, [](const VideoFrame& frame) {
        std::cout << "Lambda: 收到帧 " << frame.width << "x" << frame.height << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));
}
```

### 连接类型

该库通过 `connection_type` 枚举支持多种连接类型：

```cpp
enum connection_type {
    auto_connection = 0,            // 系统根据线程上下文自动选择直接或队列连接
    direct_connection = 1,          // 在发射信号的线程中同步执行
    queued_connection = 2,          // 在目标线程中异步执行
    blocking_queued_connection = 3, // 异步执行但阻塞等待完成
    unique_connection = 0x80,       // 确保只存在一个相同的连接（可与其他类型组合）
    singleshot_connection = 0x100   // 执行一次后自动断开连接（可与其他类型组合）
};
```

- `auto_connection`: 系统根据任务队列的情况自动选择连接类型：
  - 如果没有指定任务队列（nullptr或不指定）：
    * 始终使用直接连接
    * 在发射信号的线程中执行
  - 如果指定了任务队列：
    * 如果从队列线程发射信号，使用直接连接
    * 如果从其他线程发射信号，使用队列连接
    * 使用队列连接时在任务队列的线程中执行

- `direct_connection`: 在发射信号的线程中同步执行槽函数。这是最快的选项，但可能会阻塞发射线程。

- `queued_connection`: 槽函数的执行被加入队列，将在目标线程的事件循环中异步执行。这对于跨线程的信号槽通信是安全的。

- `blocking_queued_connection`: 类似于队列连接，但信号发射会阻塞直到槽函数执行完成。当需要槽函数执行结果时很有用。

- `unique_connection`: 可以使用OR运算符(|)与其他连接类型组合。确保相同的槽不会多次连接到同一个信号。

- `singleshot_connection`: 可以使用OR运算符(|)与其他连接类型组合。槽函数执行一次后会自动断开连接。

组合示例：
```cpp
// 唯一的队列连接
connection_type::queued_connection | connection_type::unique_connection

// 单次执行的阻塞队列连接
connection_type::blocking_queued_connection | connection_type::singleshot_connection
```

### 连接管理

```cpp
// 方法1：使用连接对象
auto conn = CONNECT(sender, signal, receiver, SLOT(Receiver::slot),
                   connection_type::auto_connection, TQ("worker"));
conn.disconnect();  // 手动断开连接

// 方法2：使用 DISCONNECT 宏
DISCONNECT(sender, signal, receiver, SLOT(Receiver::slot));

// 方法3：作用域连接（RAII）
{
    sigslot::scoped_connection conn = CONNECT(sender, signal, receiver, slot);
    // 连接在此作用域内有效
} // 离开作用域时自动断开连接

// 方法4：断开信号的所有槽连接
sender->signal.disconnect_all();
```

## 构建要求

- C++14或更高版本
- CMake 3.10或更高版本
- 支持多线程的标准库实现

## 实现细节

该库由以下几个主要组件构成：

- `signal.hpp`: 核心信号槽实现
- `signal_slot_api.hpp`: 用户友好的API宏
- `task_queue.hpp`: 异步执行的任务队列接口
- `task_queue_manager.hpp`: 任务队列管理

## 注意事项

1. 在多线程环境中使用时，需要注意信号发射和槽执行的线程安全性
2. 使用队列连接时，确保目标线程的事件循环正常运行
3. 使用弱指针跟踪时，注意对象的生命周期管理
4. 合理使用连接类型，避免不必要的性能开销

## 示例

更多使用示例请参考 `main.cpp` 和 `test_signal_slot.cpp`。 

### 完整使用指南

#### 需要包含的头文件
```cpp
#include <iostream>
#include <memory>
#include <thread>
#include "./signal-slot/signal_slot_api.hpp"
#include "./signal-slot/core/task_queue_manager.hpp"
```

#### 1. 初始化任务队列管理器
```cpp
int main() {
    // 创建一个名为"worker"的任务队列
    TQMgr->create({"worker"});
}
```

#### 2. 定义信号发送者
```cpp
class DeviceController {
public:
    // 无参数信号
    SIGNAL(started);
    
    // 单参数信号
    SIGNAL(devicePlugged, const std::shared_ptr<DeviceInfo>&);
    
    // 多参数信号
    SIGNAL(progress, int, int, const std::string&);
    
    // 基本类型参数信号
    SIGNAL(error, const std::string&);
    
    // 自定义类型参数信号
    SIGNAL(frameReceived, const VideoFrame&);
};
```

#### 3. 定义信号接收者
```cpp
class UiController {
public:
    // 无参数槽函数
    void onStarted() {
        std::cout << "设备已启动" << std::endl;
    }

    // 单参数槽函数
    void onDevicePlugged(const std::shared_ptr<DeviceInfo>& info) {
        std::cout << "设备已插入 - " << info->deviceName << std::endl;
    }

    // 多参数槽函数
    void onProgress(int current, int total, const std::string& message) {
        std::cout << "进度 " << current << "/" << total 
                 << " - " << message << std::endl;
    }
};
```

#### 4. 连接示例

1. 基本连接（不使用任务队列）
```cpp
CONNECT(dc, started, ui.get(), SLOT(UiController::onStarted),
        sigslot::connection_type::auto_connection);
```

2. 带任务队列的直接连接
```cpp
CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePlugged),
        sigslot::connection_type::direct_connection, TQ("worker"));
```

3. 队列连接
```cpp
CONNECT(dc, progress, ui.get(), SLOT(UiController::onProgress),
        sigslot::connection_type::queued_connection, TQ("worker"));
```

4. Lambda连接
```cpp
CONNECT(dc, frameReceived, [](const VideoFrame& frame) {
    std::cout << "收到帧 " << frame.width << "x" << frame.height << std::endl;
}, sigslot::connection_type::queued_connection, TQ("worker"));
```

5. 单次连接
```cpp
CONNECT(dc, error, [](const std::string& error) {
    std::cout << "单次错误处理器 - " << error << std::endl;
}, sigslot::connection_type::queued_connection | sigslot::connection_type::singleshot_connection, 
TQ("worker"));
```

6. 唯一连接
```cpp
CONNECT(dc, error, [](const std::string& error) {
    std::cout << "唯一错误处理器 - " << error << std::endl;
}, sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
TQ("worker"));
```

#### 5. 信号发射
```cpp
// 发射无参数信号
EMIT(started);

// 发射单参数信号
auto deviceInfo = std::make_shared<DeviceInfo>();
deviceInfo->deviceName = "麦克风";
EMIT(devicePlugged, deviceInfo);

// 发射多参数信号
EMIT(progress, 1, 3, "处理中...");

// 发射自定义类型信号
VideoFrame frame{640, 480, std::vector<uint8_t>(640*480, 0)};
EMIT(frameReceived, frame);
```

#### 6. 连接管理示例

1. 使用连接对象
```cpp
auto conn = CONNECT(dc, progress, [](int current, int total, const std::string& message) {
    std::cout << "进度 " << current << "/" << total << std::endl;
}, sigslot::connection_type::queued_connection, TQ("worker"));

conn.disconnect();  // 手动断开连接
```

2. 使用作用域连接
```cpp
{
    sigslot::scoped_connection conn = CONNECT(dc, error, [](const std::string& error) {
        std::cout << "作用域错误处理器：" << error << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));
    
    // 连接仅在此作用域内有效
} // 在此自动断开连接
```

3. 使用DISCONNECT宏
```cpp
DISCONNECT(dc, started, nullptr, nullptr);  // 断开'started'信号的所有槽连接
``` 

## 更多示例

如需了解更详细的示例和使用场景，可以参考以下文件：

- `examples.cpp`：展示了全面的使用方法，包括：
  - 不同的连接类型（直接连接、队列连接、自动连接、阻塞连接）
  - 各种信号槽模式（成员函数、Lambda表达式、全局函数）
  - 连接管理方法
  - 线程安全的信号发射
  - 任务队列集成
  - 线程ID跟踪调试

- `test_signal_slot.cpp`：包含了单元测试，展示了：
  - 基本的信号槽功能
  - 连接类型行为：
    * 自动连接：测试有无任务队列的行为
    * 直接连接：验证在发射线程中执行
    * 队列连接：测试异步执行
    * 阻塞队列连接：验证阻塞行为和执行时间
  - 特殊连接类型：
    * 单次连接：测试一次性执行和自动断开
    * 唯一连接：验证防止重复连接
  - 组合连接类型：
    * 测试唯一+单次+队列的组合
    * 验证标志位组合时的正确行为
  - 连接管理：
    * 手动断开连接
    * 作用域连接生命周期
    * 连接阻塞/解除阻塞
    * 断开所有槽连接
  - 线程安全方面：
    * 跨线程信号发射
    * 线程ID验证
    * 任务队列集成

这些文件提供了实用的示例和测试用例，可以帮助你理解如何在自己的项目中有效地使用这个库。 