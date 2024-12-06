#include <gtest/gtest.h>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include "../signal-slot/signal_slot_api.hpp"
#include "../signal-slot/core/task_queue_manager.hpp"

class SignalSlotTest : public ::testing::Test {
protected:
    void SetUp() override {
        TQMgr->create({"worker"});
    }

    void TearDown() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
};

// Test data structure
struct TestData {
    int value;
    std::string message;
};

// Test class for signal emission
class TestEmitter {
public:
    // Signal declarations using new C++ style API
    sigslot::Signal<> noParamSignal;
    sigslot::Signal<int> singleParamSignal;
    sigslot::Signal<int, std::string> multiParamSignal;
    sigslot::Signal<TestData> customTypeSignal;
};

// Test class for signal reception
class TestReceiver {
public:
    void onNoParam() {
        noParamCalled = true;
        executionThreadId = std::this_thread::get_id();
    }

    void onSingleParam(int value) {
        std::lock_guard<std::mutex> lock(mutex);
        executionThreadId = std::this_thread::get_id();
        lastValue = value;
        singleParamCalled = true;
        callCount++;
    }

    void onMultiParam(int value, const std::string& msg) {
        multiParamCalled = true;
        lastValue = value;
        lastMessage = msg;
        executionThreadId = std::this_thread::get_id();
    }

    void onCustomType(const TestData& data) {
        customTypeCalled = true;
        lastValue = data.value;
        lastMessage = data.message;
        executionThreadId = std::this_thread::get_id();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        noParamCalled = false;
        singleParamCalled = false;
        multiParamCalled = false;
        customTypeCalled = false;
        lastValue = 0;
        lastMessage.clear();
        executionThreadId = std::thread::id();
        callCount = 0;
    }

    bool noParamCalled = false;
    bool singleParamCalled = false;
    bool multiParamCalled = false;
    bool customTypeCalled = false;
    int lastValue = 0;
    std::string lastMessage;
    std::thread::id executionThreadId;
    int callCount = 0;
private:
    std::mutex mutex;
};

// Test basic functionality
TEST_F(SignalSlotTest, BasicUsage) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    // Connect and emit
    auto conn = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                               receiver.get(), &TestReceiver::onSingleParam);
    
    sigslot::emit(emitter->singleParamSignal, 42);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 42);
}

TEST_F(SignalSlotTest, CrossThreadEmission) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    auto conn = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                               receiver.get(), &TestReceiver::onSingleParam);

    std::thread([&]() {
        sigslot::emit(emitter->singleParamSignal, 42);
    }).join();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
}

// Test disconnection
TEST_F(SignalSlotTest, Disconnection) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    auto conn = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                               receiver.get(), &TestReceiver::onSingleParam);

    // Test before disconnection
    sigslot::emit(emitter->singleParamSignal, 42);
    EXPECT_TRUE(receiver->singleParamCalled);

    // Test after disconnection
    sigslot::disconnect(emitter.get(), emitter->singleParamSignal,
                       receiver.get(), &TestReceiver::onSingleParam);
    receiver->reset();
    sigslot::emit(emitter->singleParamSignal, 43);
    EXPECT_FALSE(receiver->singleParamCalled);
}

// Test auto connection behavior
TEST_F(SignalSlotTest, AutoConnectionBehavior) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    // Case 1: Auto connection without task queue
    auto conn1 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                receiver.get(), &TestReceiver::onSingleParam,
                                sigslot::connection_type::auto_connection);
    
    sigslot::emit(emitter->singleParamSignal, 1);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->executionThreadId, mainThreadId);  // Should execute in main thread

    // Case 2: Auto connection with task queue, emit from main thread
    receiver->reset();
    auto conn2 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                receiver.get(), &TestReceiver::onSingleParam,
                                sigslot::connection_type::auto_connection,
                                TQ("worker"));
    
    sigslot::emit(emitter->singleParamSignal, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_NE(receiver->executionThreadId, mainThreadId);  // Should execute in worker thread
}

// Test thread safety
TEST_F(SignalSlotTest, ThreadSafety) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();
    std::atomic<int> emitCount{0};
    std::atomic<int> receiveCount{0};

    // Connect with queued connection
    auto conn = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                               receiver.get(), &TestReceiver::onSingleParam,
                               sigslot::connection_type::queued_connection,
                               TQ("worker"));

    // Emit from multiple threads simultaneously
    constexpr int ThreadCount = 10;
    constexpr int EmitsPerThread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < ThreadCount; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < EmitsPerThread; ++j) {
                sigslot::emit(emitter->singleParamSignal, j);
                emitCount++;
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    // Wait for all queued signals to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Verify all signals were processed
    EXPECT_EQ(emitCount.load(), ThreadCount * EmitsPerThread);
    EXPECT_EQ(receiver->callCount, ThreadCount * EmitsPerThread);
}

// Test blocking queued connection timing
TEST_F(SignalSlotTest, BlockingQueuedTiming) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    // Add delay in slot
    constexpr int DelayMs = 100;
    auto conn = sigslot::connect(emitter.get(), emitter->singleParamSignal,
        [DelayMs](int value) {
            std::this_thread::sleep_for(std::chrono::milliseconds(DelayMs));
        },
        sigslot::connection_type::blocking_queued_connection,
        TQ("worker"));

    // Measure execution time
    auto start = std::chrono::steady_clock::now();
    sigslot::emit(emitter->singleParamSignal, 1);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(duration, DelayMs);  // Should have blocked for at least DelayMs
}

// Test unique connection
TEST_F(SignalSlotTest, UniqueConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    // First connection
    auto conn1 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                receiver.get(), &TestReceiver::onSingleParam,
                                static_cast<uint32_t>(sigslot::connection_type::unique_connection));

    // Try duplicate connection - should fail
    auto conn2 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                receiver.get(), &TestReceiver::onSingleParam,
                                static_cast<uint32_t>(sigslot::connection_type::unique_connection));

    // First emission
    sigslot::emit(emitter->singleParamSignal, 1);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->callCount, 1);

    // Reset and emit again
    receiver->reset();
    sigslot::emit(emitter->singleParamSignal, 2);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->callCount, 1);  // Still only called once
}

// Test single-shot connection
TEST_F(SignalSlotTest, SingleShotConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    // Create single-shot connection
    auto conn = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                               receiver.get(), &TestReceiver::onSingleParam,
                               static_cast<uint32_t>(sigslot::connection_type::singleshot_connection));

    // First emission - should be processed
    sigslot::emit(emitter->singleParamSignal, 1);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 1);

    // Reset and emit again - should not be processed
    receiver->reset();
    sigslot::emit(emitter->singleParamSignal, 2);
    EXPECT_FALSE(receiver->singleParamCalled);  // Should not be called
    EXPECT_EQ(receiver->callCount, 0);
}

// Test queued connection
TEST_F(SignalSlotTest, QueuedConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    auto conn = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                               receiver.get(), &TestReceiver::onSingleParam,
                               static_cast<uint32_t>(sigslot::connection_type::queued_connection),
                               TQ("worker"));

    sigslot::emit(emitter->singleParamSignal, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_NE(receiver->executionThreadId, mainThreadId);
}

/*
// Test connection type combinations
TEST_F(SignalSlotTest, ConnectionTypeCombinations) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    // Case 1: Unique + Queued connection
    auto conn1 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                  receiver.get(), &TestReceiver::onSingleParam,
                                  sigslot::connection_type::queued_connection |
                                  sigslot::connection_type::unique_connection,
                                  TQ("worker"));

    // Try duplicate connection
    auto conn2 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                  receiver.get(), &TestReceiver::onSingleParam,
                                  sigslot::connection_type::queued_connection |
                                  sigslot::connection_type::unique_connection,
                                  TQ("worker"));

    sigslot::emit(emitter->singleParamSignal, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->callCount, 1);  // Should only be called once

    // Case 2: Single-shot + Queued connection
    receiver->reset();
    auto conn3 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                  receiver.get(), &TestReceiver::onSingleParam,
                                  sigslot::connection_type::queued_connection |
                                  sigslot::connection_type::singleshot_connection,
                                  TQ("worker"));

    sigslot::emit(emitter->singleParamSignal, 2);  // Should be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);

    receiver->reset();
    sigslot::emit(emitter->singleParamSignal, 3);  // Should not be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(receiver->singleParamCalled);
}


// Test connection types
TEST_F(SignalSlotTest, ConnectionTypes) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    // Case 1: Direct connection
    auto conn1 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                  receiver.get(), &TestReceiver::onSingleParam,
                                  static_cast<uint32_t>(sigslot::connection_type::direct_connection));

    // Case 2: Queued connection
    auto conn2 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                  receiver.get(), &TestReceiver::onSingleParam,
                                  static_cast<uint32_t>(sigslot::connection_type::queued_connection),
                                  TQ("worker"));

    // Case 3: Combined connection types
    auto conn3 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                  receiver.get(), &TestReceiver::onSingleParam,
                                  static_cast<uint32_t>(sigslot::connection_type::queued_connection |
                                                        sigslot::connection_type::unique_connection),
                                  TQ("worker"));

    // Test direct connection
    sigslot::emit(emitter->singleParamSignal, 1);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 1);

    // Test queued connection
    receiver->reset();
    sigslot::emit(emitter->singleParamSignal, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 2);

    // Test combined connection types
    receiver->reset();
    sigslot::emit(emitter->singleParamSignal, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 3);

    // Try duplicate connection (should fail due to unique_connection)
    auto conn4 = sigslot::connect(emitter.get(), emitter->singleParamSignal,
                                  receiver.get(), &TestReceiver::onSingleParam,
                                  static_cast<uint32_t>(sigslot::connection_type::queued_connection |
                                                        sigslot::connection_type::unique_connection),
                                  TQ("worker"));

    receiver->reset();
    sigslot::emit(emitter->singleParamSignal, 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->callCount, 1);  // Should only be called once
}
 */