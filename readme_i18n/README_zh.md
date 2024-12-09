# 现代C++信号槽库

[English](../README.md) | [中文](README_zh.md)

## 简介

这是一个基于现代C++的信号槽实现，提供了类型安全、线程安全的事件处理机制。采用模板元编程和完美转发等现代C++特性，支持任意类型和数量的参数，无需额外的类型注册。

## 特性

- 类型安全的信号槽连接
- 支持任意数量和类型的参数
- 线程安全的信号发射
- 支持多种连接类型：
  - 自动连接 (auto_connection)
  - 直接连接 (direct_connection)
  - 队列连接 (queued_connection)
  - 阻塞队列连接 (blocking_queued_connection)
  - 唯一连接 (unique_connection)
  - 单次连接 (singleshot_connection)
- 支持Lambda表达式、成员函数、自由函数作为槽
- 基于任务队列的跨线程通信
- RAII风格的连接生命周期管理

## 使用示例

### 1. 信号定义

```cpp
class DeviceController {
public:
    // 使用模板类直接定义信号
    sigslot::Signal<> started;                                           // 无参数信号
    sigslot::Signal<int, int, const std::string&> progress;             // 多参数信号
    sigslot::Signal<const std::shared_ptr<DeviceInfo>&> devicePlugged;  // 自定义类型信号
};
```

### 2. 槽函数

```cpp
class UiController {
public:
    // 成员函数作为槽
    void onStarted() {
        std::cout << "Device started" << std::endl;
    }

    // 带参数的槽函数
    void onProgress(int current, int total, const std::string& message) {
        std::cout << "Progress " << current << "/" << total 
                 << " - " << message << std::endl;
    }
};
```

### 3. 建立连接

```cpp
auto dc = std::make_shared<DeviceController>();
auto ui = std::make_shared<UiController>();

// 直接连接
sigslot::connect(dc.get(), dc->started, ui.get(), &UiController::onStarted);

// 队列连接
sigslot::connect(dc.get(), dc->progress, ui.get(), &UiController::onProgress,
                 sigslot::connection_type::queued_connection,
                 TQ("worker"));

// Lambda连接
sigslot::connect(dc.get(), dc->devicePlugged,
    [](const std::shared_ptr<DeviceInfo>& info) {
        std::cout << "Device plugged: " << info->deviceName << std::endl;
    });
```

### 4. 信号发射

```cpp
// 发射信号
sigslot::emit(dc->started);
sigslot::emit(dc->progress, 50, 100, "Processing...");

// 发射带自定义类型的信号
auto info = std::make_shared<DeviceInfo>();
info->deviceName = "TestDevice";
sigslot::emit(dc->devicePlugged, info);
```

### 5. 连接管理

```cpp
// 使用连接对象
auto conn = sigslot::connect(dc.get(), dc->progress, ui.get(), &UiController::onProgress);
conn.disconnect();  // 手动断开

// 作用域连接
{
    sigslot::scoped_connection conn = 
        sigslot::connect(dc.get(), dc->started, ui.get(), &UiController::onStarted);
} // 离开作用域自动断开

// 断开所有连接
dc->started.disconnect_all();
```

## 线程安全

### 任务队列初始化

```cpp
int main() {
    // 创建工作线程队列
    TQMgr->create({"worker"});
    return 0;
}
```

### 跨线程通信

```cpp
// 在工作线程中安全发射信号
std::thread([dc]() {
    sigslot::emit(dc->progress, 75, 100, "From worker thread");
}).join();
```

## 连接类型说明

```cpp
enum connection_type {
    auto_connection = 0,            // 自动选择连接类型
    direct_connection = 1,          // 直接在发射线程执行
    queued_connection = 2,          // 在目标线程异步执行
    blocking_queued_connection = 3, // 异步执行但阻塞等待完成
    unique_connection = 0x80,       // 确保唯一连接
    singleshot_connection = 0x100   // 执行一次后自动断开
};
```

## 注意事项

1. 使用队列连接前需要先创建对应的任务队列
2. 跨线程通信建议使用队列连接
3. 阻塞队列连接会阻塞发射线程直到槽函数执行完成
4. 单次连接在执行一次后会自动断开
5. 唯一连接确保相同的信号和槽只能连接一次

## 构建要求

- C++14或更高版本
- CMake 3.10或更高版本
- 支持多线程的标准库实现

更多示例请参考 `examples.cpp` 和 `test/test_signal_slot.cpp`。 