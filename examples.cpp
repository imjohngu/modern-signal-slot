#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include "./signal-slot/signal_slot_api.hpp"
#include "./signal-slot/core/task_queue.hpp"
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

// Example of slot functions in a regular class
class UiController {
public:
    // Slot without parameters
    void onStarted() {
        std::cout << "UiController: Device started" << std::endl;
    }

    // Slot with single parameter
    void onDevicePlugged(const std::shared_ptr<DeviceInfo>& info) {
        std::cout << "UiController: Device plugged - " << info->deviceName << std::endl;
    }

    // Slot with multiple parameters
    void onProgress(int current, int total, const std::string& message) {
        std::cout << "UiController: Progress " << current << "/" << total 
                 << " - " << message << std::endl;
    }

    // Slot with return value (though return value will be ignored)
    bool onError(const std::string& error) {
        std::cout << "UiController: Error - " << error << std::endl;
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
    }
};

// Global function as slot
void globalProgressHandler(int current, int total, const std::string& message) {
    std::cout << "Global: Progress " << current << "/" << total 
             << " - " << message << std::endl;
}

int main()
{
    TQMgr->create({"worker"});

    auto dc = std::make_shared<DeviceController>();
    auto ui = std::make_shared<UiController>();

    // 1. Connect member function slot - auto connection
    CONNECT(dc, started, ui.get(), SLOT(UiController::onStarted),
            sigslot::connection_type::auto_connection, TQ("worker"));

    // 2. Connect member function slot - direct connection
    CONNECT(dc, devicePlugged, ui.get(), SLOT(UiController::onDevicePlugged),
            sigslot::connection_type::direct_connection, TQ("worker"));

    // 3. Connect member function slot - queued connection
    CONNECT(dc, progress, ui.get(), SLOT(UiController::onProgress),
            sigslot::connection_type::queued_connection, TQ("worker"));

    // 4. Connect member function slot - blocking queued connection
    CONNECT(dc, error, ui.get(), SLOT(UiController::onError),
            sigslot::connection_type::blocking_queued_connection, TQ("worker"));

    // 5. Connect global function slot
    CONNECT(dc, progress, globalProgressHandler,
            sigslot::connection_type::queued_connection, TQ("worker"));

    // 6. Connect lambda slot - no parameters
    CONNECT(dc, started, []() {
        std::cout << "Lambda: Device started" << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    // 7. Connect lambda slot - single parameter
    CONNECT(dc, deviceUnplugged, [](const std::shared_ptr<DeviceInfo>& info) {
        std::cout << "Lambda: Device unplugged - " << info->deviceName << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    // 8. Connect lambda slot - multiple parameters
    CONNECT(dc, progress, [](int current, int total, const std::string& message) {
        std::cout << "Lambda: Progress " << current << "/" << total 
                 << " - " << message << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    // 9. Connect lambda slot - custom type parameter
    CONNECT(dc, frameReceived, [](const VideoFrame& frame) {
        std::cout << "Lambda: Received frame " << frame.width << "x" << frame.height << std::endl;
    }, sigslot::connection_type::queued_connection, TQ("worker"));

    // 10. Single-shot connection (auto-disconnect after first trigger)
    CONNECT(dc, error, [](const std::string& error) {
        std::cout << "Lambda: One-shot error handler - " << error << std::endl;
    }, sigslot::connection_type::queued_connection | sigslot::connection_type::singleshot_connection, 
    TQ("worker"));

    // 11. Unique connection (ensures only one identical connection)
    CONNECT(dc, error, [](const std::string& error) {
        std::cout << "Lambda: Unique error handler - " << error << std::endl;
    }, sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
    TQ("worker"));

    // Execute mock operations
    dc->mockOperations();

    // Wait for tasks in queue to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return 0;
}
