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
    // Modern C++ style signal declarations
    sigslot::Signal<> started;
    sigslot::Signal<const std::shared_ptr<DeviceInfo>&> devicePlugged;
    sigslot::Signal<const std::shared_ptr<DeviceInfo>&> deviceUnplugged;
    sigslot::Signal<int, int, const std::string&> progress;
    sigslot::Signal<const std::string&> error;
    sigslot::Signal<const VideoFrame&> frameReceived;

    // Simulate device operations
    void mockOperations() {
        // Emit signal without parameters
        sigslot::emit(started);

        // Emit signal with single parameter
        auto deviceInfo = std::make_shared<DeviceInfo>();
        deviceInfo->deviceId = "uuid-12345678900987654321";
        deviceInfo->deviceName = "microphone";
        sigslot::emit(devicePlugged, deviceInfo);

        // Emit signal with multiple parameters
        for (int i = 0; i < 3; ++i) {
            sigslot::emit(progress, i, 3, "Processing...");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Emit error signal
        sigslot::emit(error, "Connection lost");

        // Emit signal with custom type
        VideoFrame frame{640, 480, std::vector<uint8_t>(640 * 480, 0)};
        sigslot::emit(frameReceived, frame);

        // Emit device unplugged signal
        sigslot::emit(deviceUnplugged, deviceInfo);

        // Demonstrate auto connection behavior in different threads
        std::cout << "\n=== Testing auto connection behavior ===" << std::endl;
        auto worker_thread = std::thread([&]() {
            sigslot::emit(progress, 10, 100, "Auto connection from worker thread");
        });
        sigslot::emit(progress, 0, 100, "Auto connection from main thread");
        worker_thread.join();

        // Demonstrate blocking queued connection
        std::cout << "\n=== Testing blocking queued connection ===" << std::endl;
        sigslot::emit(error, "This will block until all slots complete");
        std::cout << "Blocking queued connection completed" << std::endl;

        // Demonstrate combined connection types
        std::cout << "\n=== Testing combined connection types ===" << std::endl;
        sigslot::emit(error, "This will trigger unique and single-shot slots");
        sigslot::emit(error, "This should not trigger those slots again");
    }
};

void testSignalSlot() {
    std::cout << "\n=== Testing Signal-Slot Examples ===" << std::endl;

    auto dc = std::make_shared<DeviceController>();
    auto ui = std::make_shared<UiController>();

    // 1. Basic connections (default direct connection)
    auto conn1 = sigslot::connect(dc.get(), dc->started, 
                                ui.get(), &UiController::onStartedDefault);

    auto conn2 = sigslot::connect(dc.get(), dc->progress, 
                                ui.get(), &UiController::onProgressQueued);

    // 2. Lambda connections
    auto conn3 = sigslot::connect(dc.get(), dc->error,
        [](const std::string& error) {
            std::cout << get_thread_id() << "[Lambda] Error: " << error << std::endl;
        });

    // 3. Queued connections
    auto conn4 = sigslot::connect(dc.get(), dc->progress, 
                                ui.get(), &UiController::onProgressQueued,
                                sigslot::connection_type::queued_connection, 
                                TQ("worker"));

    // 4. Blocking queued connections
    auto conn5 = sigslot::connect(dc.get(), dc->error, 
                                ui.get(), &UiController::onErrorBlocking,
                                sigslot::connection_type::blocking_queued_connection, 
                                TQ("worker"));

    // Signal emissions
    sigslot::emit(dc->started);
    sigslot::emit(dc->progress, 50, 100, "Progress test");
    sigslot::emit(dc->error, "Error test");

    // Connection management examples
    // 1. Manual disconnection
    conn1.disconnect();

    // 2. Scoped connection
    {
        auto scoped_conn = sigslot::connect(dc.get(), dc->error,
            [](const std::string& error) {
                std::cout << get_thread_id() << "[Scoped] Error: " << error << std::endl;
            });
        sigslot::emit(dc->error, "Scoped connection test");
    } // Auto disconnection here

    // 3. Disconnect specific slot
    sigslot::disconnect(dc.get(), dc->progress, ui.get(), &UiController::onProgressQueued);

    // 4. Disconnect all slots
    sigslot::disconnect_all(dc->started);

    // Cross-thread examples
    std::thread([dc]() {
        sigslot::emit(dc->started);
        sigslot::emit(dc->progress, 75, 100, "From worker thread");
    }).join();

    // Wait for queued signals
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main() {
    TQMgr->create({"worker"});

    testSignalSlot();

    return 0;
}
