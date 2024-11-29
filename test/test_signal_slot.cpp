#include <gtest/gtest.h>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include "../signal-slot/signal_slot_api.hpp"
#include "./signal-slot/core/task_queue.hpp"
#include "./signal-slot/core/task_queue_manager.hpp"

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

// Test class
class TestReceiver {
public:
    void onNoParam() {
        noParamCalled = true;
    }

    void onSingleParam(int value) {
        lastValue = value;
        singleParamCalled = true;
    }

    void onMultiParam(int value, const std::string& msg) {
        lastValue = value;
        lastMessage = msg;
        multiParamCalled = true;
    }

    void onCustomType(const TestData& data) {
        lastValue = data.value;
        lastMessage = data.message;
        customTypeCalled = true;
    }

    void reset() {
        noParamCalled = false;
        singleParamCalled = false;
        multiParamCalled = false;
        customTypeCalled = false;
        lastValue = 0;
        lastMessage.clear();
    }

    bool noParamCalled = false;
    bool singleParamCalled = false;
    bool multiParamCalled = false;
    bool customTypeCalled = false;
    int lastValue = 0;
    std::string lastMessage;
};

class TestEmitter {
public:
    SIGNAL(noParamSignal);
    SIGNAL(singleParamSignal, int);
    SIGNAL(multiParamSignal, int, const std::string&);
    SIGNAL(customTypeSignal, const TestData&);
};

// Test signal without parameters
TEST_F(SignalSlotTest, NoParamSignal) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    CONNECT(emitter, noParamSignal, receiver.get(), SLOT(TestReceiver::onNoParam),
            sigslot::connection_type::direct_connection, TQ("worker"));

    EMIT(emitter->noParamSignal);
    EXPECT_TRUE(receiver->noParamCalled);
}

// Test signal with single parameter
TEST_F(SignalSlotTest, SingleParamSignal) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    CONNECT(emitter, singleParamSignal, receiver.get(), SLOT(TestReceiver::onSingleParam),
            sigslot::connection_type::direct_connection, TQ("worker"));

    EMIT(emitter->singleParamSignal, 42);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 42);
}

// Test signal with multiple parameters
TEST_F(SignalSlotTest, MultiParamSignal) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    CONNECT(emitter, multiParamSignal, receiver.get(), SLOT(TestReceiver::onMultiParam),
            sigslot::connection_type::direct_connection, TQ("worker"));

    EMIT(emitter->multiParamSignal, 42, "test");
    EXPECT_TRUE(receiver->multiParamCalled);
    EXPECT_EQ(receiver->lastValue, 42);
    EXPECT_EQ(receiver->lastMessage, "test");
}

// Test signal with custom type
TEST_F(SignalSlotTest, CustomTypeSignal) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    CONNECT(emitter, customTypeSignal, receiver.get(), SLOT(TestReceiver::onCustomType),
            sigslot::connection_type::direct_connection, TQ("worker"));

    TestData data{42, "test"};
    EMIT(emitter->customTypeSignal, data);
    EXPECT_TRUE(receiver->customTypeCalled);
    EXPECT_EQ(receiver->lastValue, 42);
    EXPECT_EQ(receiver->lastMessage, "test");
}

// Test lambda slot
TEST_F(SignalSlotTest, LambdaSlot) {
    auto emitter = std::make_shared<TestEmitter>();
    bool lambdaCalled = false;

    CONNECT(emitter, singleParamSignal, [&lambdaCalled](int value) {
        lambdaCalled = true;
    }, sigslot::connection_type::direct_connection, TQ("worker"));

    EMIT(emitter->singleParamSignal, 42);
    EXPECT_TRUE(lambdaCalled);
}

// Test single-shot connection
TEST_F(SignalSlotTest, SingleShotConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    CONNECT(emitter, singleParamSignal, receiver.get(), SLOT(TestReceiver::onSingleParam),
            sigslot::connection_type::direct_connection | sigslot::connection_type::singleshot_connection,
            TQ("worker"));

    EMIT(emitter->singleParamSignal, 42);
    EXPECT_TRUE(receiver->singleParamCalled);

    receiver->reset();
    EMIT(emitter->singleParamSignal, 43);
    EXPECT_FALSE(receiver->singleParamCalled);
}

// Test unique connection
TEST_F(SignalSlotTest, UniqueConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    int callCount = 0;

    auto slot = [&callCount](int value) { callCount++; };

    // First connection
    CONNECT(emitter, singleParamSignal, slot,
            sigslot::connection_type::direct_connection | sigslot::connection_type::unique_connection,
            TQ("worker"));

    // Try duplicate connection
    CONNECT(emitter, singleParamSignal, slot,
            sigslot::connection_type::direct_connection | sigslot::connection_type::unique_connection,
            TQ("worker"));

    EMIT(emitter->singleParamSignal, 42);
    EXPECT_EQ(callCount, 1); // Should only be called once
}

// Test manual disconnection
TEST_F(SignalSlotTest, ManualDisconnection) {
    struct Sender {
        SIGNAL(valueChanged, int);
    };
    
    struct Receiver {
        void onValueChanged(int value) { lastValue = value; }
        int lastValue = 0;
    };
    
    auto sender = std::make_shared<Sender>();
    auto receiver = std::make_shared<Receiver>();
    
    auto conn = CONNECT(sender, valueChanged, receiver.get(), 
                      SLOT(Receiver::onValueChanged),
                      sigslot::connection_type::direct_connection,
                      TQ("worker"));
                      
    EMIT(sender->valueChanged, 42);
    EXPECT_EQ(receiver->lastValue, 42);
    
    conn.disconnect();
    EMIT(sender->valueChanged, 24);
    EXPECT_EQ(receiver->lastValue, 42);
}

// Test scoped connection
TEST_F(SignalSlotTest, ScopedConnection) {
    struct Sender {
        SIGNAL(valueChanged, int);
    };
    
    struct Receiver {
        void onValueChanged(int value) { lastValue = value; }
        int lastValue = 0;
    };
    
    auto sender = std::make_shared<Sender>();
    auto receiver = std::make_shared<Receiver>();
    
    {
        sigslot::scoped_connection conn = CONNECT(sender, valueChanged, 
            receiver.get(), SLOT(Receiver::onValueChanged),
            sigslot::connection_type::direct_connection,
            TQ("worker"));
            
        EMIT(sender->valueChanged, 42);
        EXPECT_EQ(receiver->lastValue, 42);
    }
    
    EMIT(sender->valueChanged, 24);
    EXPECT_EQ(receiver->lastValue, 42);
}

// Test DISCONNECT macro
TEST_F(SignalSlotTest, DisconnectMacro) {
    struct Sender {
        SIGNAL(valueChanged, int);
    };
    
    struct Receiver {
        void onValueChanged(int value) { lastValue = value; }
        int lastValue = 0;
    };
    
    auto sender = std::make_shared<Sender>();
    auto receiver = std::make_shared<Receiver>();
    
    CONNECT(sender, valueChanged, receiver.get(), 
           SLOT(Receiver::onValueChanged),
           sigslot::connection_type::direct_connection,
           TQ("worker"));
           
    EMIT(sender->valueChanged, 42);
    EXPECT_EQ(receiver->lastValue, 42);
    
    DISCONNECT(sender, valueChanged, receiver.get(), SLOT(Receiver::onValueChanged));
    EMIT(sender->valueChanged, 24);
    EXPECT_EQ(receiver->lastValue, 42);
}

// Test disconnect all slots
TEST_F(SignalSlotTest, DisconnectAll) {
    struct Sender {
        SIGNAL(valueChanged, int);
    };
    
    struct Receiver {
        void onValueChanged(int value) { lastValue = value; }
        int lastValue = 0;
    };
    
    auto sender = std::make_shared<Sender>();
    auto receiver1 = std::make_shared<Receiver>();
    auto receiver2 = std::make_shared<Receiver>();
    
    CONNECT(sender, valueChanged, receiver1.get(), 
           SLOT(Receiver::onValueChanged),
           sigslot::connection_type::direct_connection,
           TQ("worker"));
           
    CONNECT(sender, valueChanged, receiver2.get(), 
           SLOT(Receiver::onValueChanged),
           sigslot::connection_type::direct_connection,
           TQ("worker"));
           
    EMIT(sender->valueChanged, 42);
    EXPECT_EQ(receiver1->lastValue, 42);
    EXPECT_EQ(receiver2->lastValue, 42);
    
    sender->valueChanged.disconnect_all();
    EMIT(sender->valueChanged, 24);
    EXPECT_EQ(receiver1->lastValue, 42);
    EXPECT_EQ(receiver2->lastValue, 42);
}

// Test auto connection type
TEST_F(SignalSlotTest, AutoConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    // Test direct connection
    CONNECT(emitter, singleParamSignal, receiver.get(), SLOT(TestReceiver::onSingleParam),
            sigslot::connection_type::direct_connection, TQ("worker"));

    // Emit from current thread - should execute directly
    EMIT(emitter->singleParamSignal, 42);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 42);

    // Reset receiver state
    receiver->reset();

    // Test queued connection
    CONNECT(emitter, singleParamSignal, receiver.get(), SLOT(TestReceiver::onSingleParam),
            sigslot::connection_type::queued_connection, TQ("worker"));

    // Emit from another thread - should execute through queue
    std::thread t([emitter]() {
        EMIT(emitter->singleParamSignal, 43);
    });
    t.join();
    
    // Wait for queue execution to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 43);

    // Finally test auto connection
    receiver->reset();
    CONNECT(emitter, singleParamSignal, receiver.get(), SLOT(TestReceiver::onSingleParam),
            sigslot::connection_type::auto_connection, TQ("worker"));

    // Emit from current thread - should automatically choose direct connection
    EMIT(emitter->singleParamSignal, 44);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 44);
}

// Test combined connection types
TEST_F(SignalSlotTest, CombinedConnectionTypes) {
    auto emitter = std::make_shared<TestEmitter>();
    int callCount = 0;

    // Store lambda function to ensure using the same function object
    auto handler = [&callCount](int value) {
        callCount++;
    };

    // First connection
    CONNECT(emitter, singleParamSignal, handler,
           sigslot::connection_type::queued_connection | 
           sigslot::connection_type::unique_connection |
           sigslot::connection_type::singleshot_connection,
           TQ("worker"));

    // Try to connect the same handler again
    CONNECT(emitter, singleParamSignal, handler,
           sigslot::connection_type::queued_connection | 
           sigslot::connection_type::unique_connection |
           sigslot::connection_type::singleshot_connection,
           TQ("worker"));

    // Emit signal and wait for queue execution
    EMIT(emitter->singleParamSignal, 42);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(callCount, 1);  // Should be called only once

    // Emit signal again
    EMIT(emitter->singleParamSignal, 43);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(callCount, 1);  // Should not be called again due to single-shot
}

// Test blocking queued connection
TEST_F(SignalSlotTest, BlockingQueuedConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    bool slotExecuted = false;
    std::thread::id slotThreadId;

    CONNECT(emitter, singleParamSignal, [&](int value) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        slotExecuted = true;
        slotThreadId = std::this_thread::get_id();
    }, sigslot::connection_type::blocking_queued_connection, TQ("worker"));

    auto mainThreadId = std::this_thread::get_id();
    EMIT(emitter->singleParamSignal, 42);

    EXPECT_TRUE(slotExecuted);
    EXPECT_NE(slotThreadId, mainThreadId);
}

// Test connection blocking
TEST_F(SignalSlotTest, ConnectionBlocking) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    auto conn = CONNECT(emitter, singleParamSignal, receiver.get(), 
                       SLOT(TestReceiver::onSingleParam),
                       sigslot::connection_type::direct_connection, TQ("worker"));

    conn.block();
    EMIT(emitter->singleParamSignal, 42);
    EXPECT_FALSE(receiver->singleParamCalled);

    conn.unblock();
    EMIT(emitter->singleParamSignal, 43);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 43);
} 