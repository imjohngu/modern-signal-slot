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

```cpp
// 自动连接（系统根据线程上下文决定）
CONNECT(dc, started, ui.get(), SLOT(UiController::onStarted),
        sigslot::connection_type::auto_connection, TQ("worker"));

// 直接连接（同步执行）
CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePlugged),
        sigslot::connection_type::direct_connection, TQ("worker"));

// 队列连接（异步执行）
CONNECT(dc, progress, ui.get(), SLOT(UiController::onProgress),
        sigslot::connection_type::queued_connection, TQ("worker"));

// 阻塞队列连接（异步但等待完成）
CONNECT(dc, error, ui.get(), SLOT(UiController::onError),
        sigslot::connection_type::blocking_queued_connection, TQ("worker"));

// 单次连接（执行一次后自动断开）
CONNECT(dc, error, errorHandler,
        sigslot::connection_type::queued_connection | sigslot::connection_type::singleshot_connection,
        TQ("worker"));

// 唯一连接（防止重复连接）
CONNECT(dc, error, errorHandler,
        sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
        TQ("worker"));
```

### 连接管理

```cpp
// RAII连接管理
auto conn = sig.connect(slot);
conn.disconnect(); // 手动断开连接

// 作用域连接
{
    sigslot::scoped_connection conn = sig.connect(slot);
    // 作用域结束时自动断开连接
}
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