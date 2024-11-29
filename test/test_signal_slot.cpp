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

// 测试数据结构
struct TestData {
    int value;
    std::string message;
};

// 测试类
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

// 测试无参数信号
TEST_F(SignalSlotTest, NoParamSignal) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    CONNECT(emitter, noParamSignal, receiver.get(), SLOT(TestReceiver::onNoParam),
            sigslot::connection_type::direct_connection, TQ("worker"));

    EMIT(emitter->noParamSignal);
    EXPECT_TRUE(receiver->noParamCalled);
}

// 测试单参数信号
TEST_F(SignalSlotTest, SingleParamSignal) {
    auto emitter = std::make_shared<TestEmitter>();
    auto receiver = std::make_shared<TestReceiver>();

    CONNECT(emitter, singleParamSignal, receiver.get(), SLOT(TestReceiver::onSingleParam),
            sigslot::connection_type::direct_connection, TQ("worker"));

    EMIT(emitter->singleParamSignal, 42);
    EXPECT_TRUE(receiver->singleParamCalled);
    EXPECT_EQ(receiver->lastValue, 42);
}

// 测试多参数信号
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

// 测试自定义类型信号
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

// 测试Lambda槽函数
TEST_F(SignalSlotTest, LambdaSlot) {
    auto emitter = std::make_shared<TestEmitter>();
    bool lambdaCalled = false;

    CONNECT(emitter, singleParamSignal, [&lambdaCalled](int value) {
        lambdaCalled = true;
    }, sigslot::connection_type::direct_connection, TQ("worker"));

    EMIT(emitter->singleParamSignal, 42);
    EXPECT_TRUE(lambdaCalled);
}

// 测试单次连接
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

// 测试唯一连接
TEST_F(SignalSlotTest, UniqueConnection) {
    auto emitter = std::make_shared<TestEmitter>();
    int callCount = 0;

    auto slot = [&callCount](int value) { callCount++; };

    // 第一次连接
    CONNECT(emitter, singleParamSignal, slot,
            sigslot::connection_type::direct_connection | sigslot::connection_type::unique_connection,
            TQ("worker"));

    // 尝试重复连接
    CONNECT(emitter, singleParamSignal, slot,
            sigslot::connection_type::direct_connection | sigslot::connection_type::unique_connection,
            TQ("worker"));

    EMIT(emitter->singleParamSignal, 42);
    EXPECT_EQ(callCount, 1); // 应该只被调用一次
} 