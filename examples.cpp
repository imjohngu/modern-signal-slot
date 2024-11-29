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

    return 0;
}
