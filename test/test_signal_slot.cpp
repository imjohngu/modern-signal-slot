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

// Test connection types behavior
class ConnectionTypesTest : public ::testing::Test {
protected:
    void SetUp() override {
        TQMgr->create({"worker"});
    }

    void TearDown() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    struct TestSignalEmitter {
        SIGNAL(testSignal, int);
    };

    struct TestSlotReceiver {
        void onSignal(int value) {
            executionThreadId = std::this_thread::get_id();
            lastValue = value;
            callCount++;
            // Add delay to test blocking behavior
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            executed = true;
        }

        void reset() {
            executed = false;
            callCount = 0;
            lastValue = 0;
            executionThreadId = std::thread::id();
        }

        bool executed = false;
        int callCount = 0;
        int lastValue = 0;
        std::thread::id executionThreadId;
    };
};

// Test auto_connection behavior
TEST_F(ConnectionTypesTest, AutoConnection) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    // Case 1: No queue specified - should always use direct connection
    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::auto_connection);
    EMIT(emitter->testSignal, 1);
    EXPECT_TRUE(receiver->executed);
    EXPECT_EQ(receiver->executionThreadId, mainThreadId); // Should execute in emitting thread

    // Case 2: With queue, emit from non-queue thread
    receiver->reset();
    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::auto_connection, TQ("worker"));
    EMIT(emitter->testSignal, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->executed);
    EXPECT_NE(receiver->executionThreadId, mainThreadId); // Should execute in worker thread

    // Case 3: With queue, emit from queue thread
    receiver->reset();
    auto workerThreadId = std::thread::id();
    TQ("worker")->PostTask([&]() {
        workerThreadId = std::this_thread::get_id();
        EMIT(emitter->testSignal, 3);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->executed);
    EXPECT_EQ(receiver->executionThreadId, workerThreadId); // Should execute directly in worker thread
}

// Test direct_connection behavior
TEST_F(ConnectionTypesTest, DirectConnection) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::direct_connection, TQ("worker"));

    // Emit from main thread
    EMIT(emitter->testSignal, 1);
    EXPECT_TRUE(receiver->executed);
    EXPECT_EQ(receiver->executionThreadId, mainThreadId); // Should always execute in emitting thread

    // Reset and emit from another thread
    receiver->reset();
    std::thread([emitter, &receiver]() {
        auto threadId = std::this_thread::get_id();
        EMIT(emitter->testSignal, 2);
        EXPECT_EQ(receiver->executionThreadId, threadId); // Should execute in emitting thread
    }).join();
}

// Test queued_connection behavior
TEST_F(ConnectionTypesTest, QueuedConnection) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::queued_connection, TQ("worker"));

    // Emit and verify asynchronous execution
    EMIT(emitter->testSignal, 1);
    EXPECT_FALSE(receiver->executed); // Should not execute immediately
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->executed);
    EXPECT_NE(receiver->executionThreadId, mainThreadId); // Should execute in worker thread
}

// Test blocking_queued_connection behavior
TEST_F(ConnectionTypesTest, BlockingQueuedConnection) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();
    auto mainThreadId = std::this_thread::get_id();
    auto startTime = std::chrono::steady_clock::now();

    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::blocking_queued_connection, TQ("worker"));

    // Emit and verify blocking behavior
    EMIT(emitter->testSignal, 1);
    auto duration = std::chrono::steady_clock::now() - startTime;
    EXPECT_TRUE(receiver->executed);
    EXPECT_NE(receiver->executionThreadId, mainThreadId); // Should execute in worker thread
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(), 50); // Should block
}

// Test unique_connection behavior
TEST_F(ConnectionTypesTest, UniqueConnection) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();

    // First connection
    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
            TQ("worker"));

    // Try duplicate connection
    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::queued_connection | sigslot::connection_type::unique_connection,
            TQ("worker"));

    EMIT(emitter->testSignal, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(receiver->callCount, 1); // Should only be called once
}

// Test singleshot_connection behavior
TEST_F(ConnectionTypesTest, SingleshotConnection) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();

    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::queued_connection | sigslot::connection_type::singleshot_connection,
            TQ("worker"));

    // First emission
    EMIT(emitter->testSignal, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->executed);
    EXPECT_EQ(receiver->callCount, 1);

    // Second emission should not trigger the slot
    receiver->reset();
    EMIT(emitter->testSignal, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(receiver->executed);
    EXPECT_EQ(receiver->callCount, 0);
}

// Test combined connection types
TEST_F(ConnectionTypesTest, CombinedConnectionTypes) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();

    // Combine unique, singleshot, and queued connection
    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::queued_connection | 
            sigslot::connection_type::unique_connection |
            sigslot::connection_type::singleshot_connection,
            TQ("worker"));

    // Try duplicate connection
    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal),
            sigslot::connection_type::queued_connection | 
            sigslot::connection_type::unique_connection |
            sigslot::connection_type::singleshot_connection,
            TQ("worker"));

    // First emission
    EMIT(emitter->testSignal, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(receiver->callCount, 1); // Should be called once

    // Second emission should not trigger due to singleshot
    receiver->reset();
    EMIT(emitter->testSignal, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(receiver->callCount, 0); // Should not be called
} 

// 在 SignalSlotTest 中添加默认连接测试
TEST_F(SignalSlotTest, DefaultConnection) {
    struct Sender {
        SIGNAL(valueChanged, int);
    };
    
    struct Receiver {
        void onValueChanged(int value) { 
            executionThreadId = std::this_thread::get_id();
            lastValue = value; 
        }
        int lastValue = 0;
        std::thread::id executionThreadId;
    };
    
    auto sender = std::make_shared<Sender>();
    auto receiver = std::make_shared<Receiver>();
    auto mainThreadId = std::this_thread::get_id();
    
    // 1. 成员函数默认连接（不指定类型和队列）
    CONNECT(sender, valueChanged, receiver.get(), SLOT(Receiver::onValueChanged));
    EMIT(sender->valueChanged, 42);
    EXPECT_EQ(receiver->lastValue, 42);
    EXPECT_EQ(receiver->executionThreadId, mainThreadId); // 应该在主线程执行
    
    // 2. Lambda默认连接
    int lambdaValue = 0;
    std::thread::id lambdaThreadId;
    CONNECT(sender, valueChanged, [&](int value) {
        lambdaValue = value;
        lambdaThreadId = std::this_thread::get_id();
    });
    EMIT(sender->valueChanged, 43);
    EXPECT_EQ(lambdaValue, 43);
    EXPECT_EQ(lambdaThreadId, mainThreadId); // 应该在主线程执行
    
    // 3. 全局函数默认连接
    int globalValue = 0;
    std::thread::id globalThreadId;
    auto globalHandler = [&](int value) {
        globalValue = value;
        globalThreadId = std::this_thread::get_id();
    };
    CONNECT(sender, valueChanged, globalHandler);
    EMIT(sender->valueChanged, 44);
    EXPECT_EQ(globalValue, 44);
    EXPECT_EQ(globalThreadId, mainThreadId); // 应该在主线程执行
}

// 在 ConnectionTypesTest 中添加默认连接行为测试
TEST_F(ConnectionTypesTest, DefaultConnectionBehavior) {
    auto emitter = std::make_shared<TestSignalEmitter>();
    auto receiver = std::make_shared<TestSlotReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    // 1. 成员函数默认连接
    CONNECT(emitter, testSignal, receiver.get(), SLOT(TestSlotReceiver::onSignal));
    EMIT(emitter->testSignal, 1);
    EXPECT_TRUE(receiver->executed);
    EXPECT_EQ(receiver->executionThreadId, mainThreadId); // 应该在主线程执行
    EXPECT_EQ(receiver->lastValue, 1);

    // 2. Lambda默认连接
    receiver->reset();
    bool lambdaExecuted = false;
    std::thread::id lambdaThreadId;
    int lambdaValue = 0;
    CONNECT(emitter, testSignal, [&](int value) {
        lambdaExecuted = true;
        lambdaThreadId = std::this_thread::get_id();
        lambdaValue = value;
    });
    EMIT(emitter->testSignal, 2);
    EXPECT_TRUE(lambdaExecuted);
    EXPECT_EQ(lambdaThreadId, mainThreadId); // 应该在主线程执行
    EXPECT_EQ(lambdaValue, 2);

    // 3. 从不同线程发射信号
    receiver->reset();
    lambdaExecuted = false;
    std::thread([emitter]() {
        EMIT(emitter->testSignal, 3);
    }).join();

    // 等待槽函数执行完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(lambdaExecuted);
    EXPECT_NE(lambdaThreadId, mainThreadId); // 应该在worker线程执行
    EXPECT_EQ(lambdaValue, 3);

    // 4. 与显式直接连接比较
    receiver->reset();
    lambdaExecuted = false;
    EMIT(emitter->testSignal, 4);
    EXPECT_TRUE(lambdaExecuted);
    EXPECT_EQ(lambdaThreadId, mainThreadId); // 行为应该与默认连接相同
    EXPECT_EQ(lambdaValue, 4);

    // 5. 测试不同线程的默认连接行为
    receiver->reset();
    TQ("worker")->PostTask([&]() {
        auto workerThreadId = std::this_thread::get_id();
        EMIT(emitter->testSignal, 5);
        EXPECT_TRUE(receiver->executed);
        EXPECT_EQ(receiver->executionThreadId, workerThreadId); // 应该在worker线程执行
        EXPECT_EQ(receiver->lastValue, 5);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
} 

// Test disconnection behaviors
TEST_F(SignalSlotTest, DisconnectionBehaviors) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();
    auto mainThreadId = std::this_thread::get_id();

    // 1. Manual disconnection using connection object
    auto conn1 = CONNECT(emitter, singleParamSignal, receiver.get(), 
                        SLOT(TestReceiver::onSingleParam));
    EMIT(emitter->singleParamSignal, 1);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 1);

    conn1.disconnect();  // Manual disconnection
    receiver->reset();
    EMIT(emitter->singleParamSignal, 2);
    EXPECT_FALSE(receiver->singleParamCalled);  // Should not be called after disconnection

    // 2. Scoped connection (RAII)
    {
        receiver->reset();
        sigslot::scoped_connection scoped = 
            CONNECT(emitter, singleParamSignal, receiver.get(), 
                   SLOT(TestReceiver::onSingleParam));
        EMIT(emitter->singleParamSignal, 3);
        EXPECT_TRUE(receiver->singleParamCalled);
        EXPECT_EQ(receiver->lastValue, 3);
    }  // Automatic disconnection at scope end
    receiver->reset();
    EMIT(emitter->singleParamSignal, 4);
    EXPECT_FALSE(receiver->singleParamCalled);

    // 3. Connection blocking/unblocking
    auto conn2 = CONNECT(emitter, singleParamSignal, receiver.get(), 
                        SLOT(TestReceiver::onSingleParam));
    conn2.block();  // Block connection
    EMIT(emitter->singleParamSignal, 5);
    EXPECT_FALSE(receiver->singleParamCalled);

    conn2.unblock();  // Unblock connection
    EMIT(emitter->singleParamSignal, 6);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 6);

    // 4. Disconnect specific receiver
    receiver->reset();
    auto receiver2 = std::make_shared<TestReceiver>();
    
    CONNECT(emitter, singleParamSignal, receiver.get(), 
            SLOT(TestReceiver::onSingleParam));
    CONNECT(emitter, singleParamSignal, receiver2.get(), 
            SLOT(TestReceiver::onSingleParam));

    EMIT(emitter->singleParamSignal, 7);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_TRUE(receiver2->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 7);
    EXPECT_EQ(receiver2->lastValue, 7);

    // Disconnect only receiver1
    emitter->singleParamSignal.disconnect(receiver.get());
    receiver->reset();
    receiver2->reset();
    EMIT(emitter->singleParamSignal, 8);
    EXPECT_FALSE(receiver->singleParamCalled);  // Should not be called
    EXPECT_TRUE(receiver2->singleParamCalled);  // Should still be called
    EXPECT_EQ(receiver2->lastValue, 8);

    // 5. Disconnect all slots
    receiver2->reset();
    CONNECT(emitter, singleParamSignal, [](int value) {
        // Lambda slot
    });
    emitter->singleParamSignal.disconnect_all();
    EMIT(emitter->singleParamSignal, 9);
    EXPECT_FALSE(receiver->singleParamCalled);
    EXPECT_FALSE(receiver2->singleParamCalled);

    // 6. Test disconnection with queued signals
    auto conn3 = CONNECT(emitter, singleParamSignal, receiver.get(), 
                        SLOT(TestReceiver::onSingleParam),
                        sigslot::connection_type::queued_connection, TQ("worker"));
    
    EMIT(emitter->singleParamSignal, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 10);

    conn3.disconnect();
    receiver->reset();
    EMIT(emitter->singleParamSignal, 11);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(receiver->singleParamCalled);  // Should not be called after disconnection
} 