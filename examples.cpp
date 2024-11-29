#include <iostream>
#include <memory>
#include <thread>
#include "./signal-slot/signal_slot_api.hpp"
#include "./signal-slot/core/task_queue_manager.hpp"

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

// 在文件开头添加辅助函数
namespace {
    std::string get_thread_id() {
        std::stringstream ss;
        ss << "[Thread " << std::this_thread::get_id() << "] ";
        return ss.str();
    }
}

// Example of slot functions in a regular class
class UiController {
public:
    // Auto connection slots
    void onStartedDefault() {
        std::cout << get_thread_id() << "[Default Auto] Device started" << std::endl;
    }

    void onStartedDirect() {
        std::cout << get_thread_id() << "[Direct Auto] Device started" << std::endl;
    }

    void onStartedWorker() {
        std::cout << get_thread_id() << "[Worker Auto] Device started" << std::endl;
    }

    // Connection type test slots
    void onStartedAuto() {
        std::cout << get_thread_id() << "[Auto] Device started" << std::endl;
    }

    void onDevicePluggedDirect(const std::shared_ptr<DeviceInfo>& info) {
        std::cout << get_thread_id() << "[Direct] Device plugged - " << info->deviceName << std::endl;
    }

    void onProgressQueued(int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[Queued] Progress " << current << "/" << total 
                 << " - " << message << std::endl;
    }

    bool onErrorBlocking(const std::string& error) {
        std::cout << get_thread_id() << "[Blocking] Error - " << error << std::endl;
        return false;
    }
};

// Example of signal emitter
class DeviceController {
public:
    // Signal without parameters
    SIGNAL(started);
    
    // Signal with single parameter
    SIGNAL(devicePlugged, const std::shared_ptr<DeviceInfo>&);
    SIGNAL(deviceUnplugged, const std::shared_ptr<DeviceInfo>&);
    
    // Signal with multiple parameters
    SIGNAL(progress, int, int, const std::string&);
    
    // Signal with basic type parameter
    SIGNAL(error, const std::string&);
    
    // Signal with custom type parameter
    SIGNAL(frameReceived, const VideoFrame&);

    // Simulate device operations
    void mockOperations() {
        // Emit signal without parameters
        EMIT(started);

        // Emit signal with single parameter
        auto deviceInfo = std::make_shared<DeviceInfo>();
        deviceInfo->deviceId = "uuid-12345678900987654321";
        deviceInfo->deviceName = "microphone";
        EMIT(devicePlugged, deviceInfo);

        // Emit signal with multiple parameters
        for (int i = 0; i < 3; ++i) {
            EMIT(progress, i, 3, "Processing...");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Emit error signal
        EMIT(error, "Connection lost");

        // Emit signal with custom type
        VideoFrame frame{640, 480, std::vector<uint8_t>(640*480, 0)};
        EMIT(frameReceived, frame);

        // Emit device unplugged signal
        EMIT(deviceUnplugged, deviceInfo);

        // Demonstrate auto connection behavior in different threads
        std::cout << "\n=== Testing auto connection behavior ===" << std::endl;
        auto worker_thread = std::thread([this]() {
            EMIT(progress, 10, 100, "Auto connection from worker thread");
        });
        EMIT(progress, 0, 100, "Auto connection from main thread");
        worker_thread.join();

        // Demonstrate blocking queued connection
        std::cout << "\n=== Testing blocking queued connection ===" << std::endl;
        EMIT(error, "This will block until all slots complete");
        std::cout << "Blocking queued connection completed" << std::endl;

        // Demonstrate combined connection types
        std::cout << "\n=== Testing combined connection types ===" << std::endl;
        EMIT(error, "This will trigger unique and single-shot slots");
        EMIT(error, "This should not trigger those slots again");
    }
};

// Global function slots
void globalProgressHandler(int current, int total, const std::string& message) {
    std::cout << get_thread_id() << "[Global] Progress " << current << "/" << total 
             << " - " << message << std::endl;
}

int main()
{
    TQMgr->create({"worker"});

    auto dc = std::make_shared<DeviceController>();
    auto ui = std::make_shared<UiController>();

    // Default connection examples (without specifying connection type)
    std::cout << "\n=== Testing default connections (without connection type) ===" << std::endl;

    // 1. Member function slots
    // No parameter
    CONNECT(dc, started, ui.get(), SLOT(UiController::onStartedDefault));

    // Single parameter
    CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePluggedDirect));

    // Multiple parameters
    CONNECT(dc, progress, ui.get(), SLOT(UiController::onProgressQueued));

    // 2. Global function slots
    // Multiple parameters
    CONNECT(dc, progress, globalProgressHandler);

    // 3. Lambda slots
    // No parameter
    CONNECT(dc, started, []() {
        std::cout << get_thread_id() << "[Default Lambda] No parameter" << std::endl;
    });

    // Single parameter
    CONNECT(dc, error, [](const std::string& error) {
        std::cout << get_thread_id() << "[Default Lambda] Single parameter: " << error << std::endl;
    });

    // Multiple parameters
    CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[Default Lambda] Multiple parameters: " 
                 << current << "/" << total << " - " << message << std::endl;
    });

    // Custom type parameter
    CONNECT(dc, frameReceived, [](const VideoFrame& frame) {
        std::cout << get_thread_id() << "[Default Lambda] Custom type: " 
                 << frame.width << "x" << frame.height << std::endl;
    });

    // Test default connections
    std::cout << "\n=== Emitting signals for default connections ===" << std::endl;
    
    // Test no parameter signal
    EMIT(dc->started);
    
    // Test single parameter signal
    auto deviceInfo = std::make_shared<DeviceInfo>();
    deviceInfo->deviceName = "default-device";
    EMIT(dc->devicePlugged, deviceInfo);
    
    // Test multiple parameters signal
    EMIT(dc->progress, 1, 10, "Default connection test");
    
    // Test error signal
    EMIT(dc->error, "Default connection error");
    
    // Test custom type signal
    VideoFrame frame{1280, 720, std::vector<uint8_t>(1280*720, 0)};
    EMIT(dc->frameReceived, frame);

    std::cout << "\n=== Default connection tests completed ===" << std::endl << std::endl;

    // Auto connection tests
    CONNECT(dc, started, ui.get(), SLOT(UiController::onStartedDefault),
            sigslot::connection_type::auto_connection);

    CONNECT(dc, started, ui.get(), SLOT(UiController::onStartedDirect),
            sigslot::connection_type::auto_connection, nullptr);

    CONNECT(dc, started, ui.get(), SLOT(UiController::onStartedWorker),
            sigslot::connection_type::auto_connection, TQ("worker"));

    // Connection type tests
    CONNECT(dc, started, ui.get(), SLOT(UiController::onStartedAuto),
            sigslot::connection_type::auto_connection, TQ("worker"));

    CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePluggedDirect),
            sigslot::connection_type::direct_connection, TQ("worker"));

    CONNECT(dc, progress, ui.get(), SLOT(UiController::onProgressQueued),
            sigslot::connection_type::queued_connection, TQ("worker"));

    CONNECT(dc, error, ui.get(), SLOT(UiController::onErrorBlocking),
            sigslot::connection_type::blocking_queued_connection, TQ("worker"));

    // Global function connection
    CONNECT(dc, progress, globalProgressHandler,
            sigslot::connection_type::queued_connection, TQ("worker"));

    // Lambda connections
    CONNECT(dc, started, []() {
        std::cout << get_thread_id() << "[Lambda] Device started" << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    CONNECT(dc, deviceUnplugged, [](const std::shared_ptr<DeviceInfo>& info) {
        std::cout << get_thread_id() << "[Lambda] Device unplugged - " << info->deviceName << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[Lambda] Progress " << current << "/" << total 
                 << " - " << message << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    CONNECT(dc, frameReceived, [](const VideoFrame& frame) {
        std::cout << get_thread_id() << "[Lambda] Frame received " << frame.width << "x" << frame.height << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    // Single-shot connection
    CONNECT(dc, error, [](const std::string& error) {
        std::cout << get_thread_id() << "[Single-shot] Error - " << error << std::endl;
    }, sigslot::connection_type::queued_connection | sigslot::connection_type::singleshot_connection, 
    TQ("worker"));

    // Unique connection
    CONNECT(dc, error, [](const std::string& error) {
        std::cout << get_thread_id() << "[Unique] Error - " << error << std::endl;
    }, sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
    TQ("worker"));

    // Connection management examples
    auto conn = CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[Manual Disconnect] Progress " << current << "/" << total << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    sigslot::scoped_connection scoped_conn = CONNECT(dc, error, [](const std::string& error) {
        std::cout << get_thread_id() << "[Scoped] Error - " << error << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    CONNECT(dc, started, []() {
        std::cout << get_thread_id() << "[To Disconnect] Device started" << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    // Scoped connection example
    {
        sigslot::scoped_connection temp_conn = CONNECT(dc, devicePlugged, 
            [](const std::shared_ptr<DeviceInfo>& info) {
            std::cout << get_thread_id() << "[Temporary] Device plugged - " << info->deviceName << std::endl;
        }, sigslot::connection_type::queued_connection, TQ("worker"));
    }

    // Combined connection types
    CONNECT(dc, error, [](const std::string& error) {
        std::cout << get_thread_id() << "[Combined] Unique and single-shot error - " << error << std::endl;
    }, sigslot::connection_type::queued_connection | 
       sigslot::connection_type::unique_connection |
       sigslot::connection_type::singleshot_connection,
    TQ("worker"));

    // Connection blocking example
    auto blocked_conn = CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[Blocked] Progress " << current << "/" << total << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    // Execute mock operations
    dc->mockOperations();

    // Wait for tasks in queue to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "\n=== Testing disconnection examples ===" << std::endl;

    // 1. 使用连接对象手动断开
    auto conn1 = CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[Manual] Progress " << current << "/" << total << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    EMIT(dc->progress, 0, 100, "Before disconnect");
    conn1.disconnect();  // 手动断开连接
    EMIT(dc->progress, 1, 100, "After disconnect");  // 这条消息不会被处理

    // 2. 使用作用域连接（RAII）
    {
        std::cout << "\n--- Testing scoped connection ---" << std::endl;
        sigslot::scoped_connection scoped = CONNECT(dc, error, [](const std::string& error) {
            std::cout << get_thread_id() << "[Scoped] Error: " << error << std::endl;
        }, sigslot::connection_type::queued_connection, TQ("worker"));

        EMIT(dc->error, "Inside scope");  // 会被处理
    }  // 离开作用域时自动断开
    EMIT(dc->error, "Outside scope");  // 不会被处理

    // 3. 使用连接阻塞/解除阻塞
    std::cout << "\n--- Testing connection blocking ---" << std::endl;
    auto conn2 = CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[Blocked] Progress " << current << "/" << total << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    EMIT(dc->progress, 2, 100, "Before blocking");
    conn2.block();  // 阻塞连接
    EMIT(dc->progress, 3, 100, "During blocking");  // 不会被处理
    conn2.unblock();  // 解除阻塞
    EMIT(dc->progress, 4, 100, "After unblocking");

    // 4. 断开特定接收者的所有连接
    std::cout << "\n--- Testing disconnect specific receiver ---" << std::endl;
    auto receiver = std::make_shared<UiController>();
    CONNECT(dc, started, receiver.get(), SLOT(UiController::onStartedDefault),
            sigslot::connection_type::queued_connection, TQ("worker"));
    CONNECT(dc, error, receiver.get(), SLOT(UiController::onErrorBlocking),
            sigslot::connection_type::queued_connection, TQ("worker"));

    EMIT(dc->started);  // 会被处理
    EMIT(dc->error, "Before disconnect");  // 会被处理
    
    // 断开receiver的所有连接
    dc->started.disconnect(receiver.get());
    dc->error.disconnect(receiver.get());
    
    EMIT(dc->started);  // 不会被处理
    EMIT(dc->error, "After disconnect");  // 不会被处理

    // 5. 断开信号的所有连接
    std::cout << "\n--- Testing disconnect all slots ---" << std::endl;
    CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[All1] Progress " << current << "/" << total << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << get_thread_id() << "[All2] Progress " << current << "/" << total << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    EMIT(dc->progress, 5, 100, "Before disconnect all");  // 两个槽都会处理
    dc->progress.disconnect_all();  // 断开所有连接
    EMIT(dc->progress, 6, 100, "After disconnect all");  // 没有槽会处理

    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "=== Disconnection tests completed ===" << std::endl;

    return 0;
}
