#pragma once

#include <string>
#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <utility>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include "queued_task.hpp"
#include "task_queue_base.hpp"

namespace core {
    class TaskQueueStdlib final : public TaskQueueBase {
    public:
        TaskQueueStdlib(std::string_view queue_name);
        ~TaskQueueStdlib() override;

        void Delete() override;
        void PostTask(std::unique_ptr<QueuedTask> task) override;
        void PostDelayedTask(std::unique_ptr<QueuedTask> task, std::chrono::milliseconds delay) override;
        void PostDelayedHighPrecisionTask(std::unique_ptr<QueuedTask> task, std::chrono::milliseconds delay) override;
        const std::string& Name() const override;

    private:
        using OrderId = uint64_t;
        using TimePoint = std::chrono::steady_clock::time_point;

        struct DelayedEntryTimeout {
            TimePoint next_fire_at;
            OrderId order{};

            bool operator<(const DelayedEntryTimeout& o) const {
                return std::tie(next_fire_at, order) < std::tie(o.next_fire_at, o.order);
            }
        };

        struct NextTask {
            bool final_task{false};
            std::unique_ptr<QueuedTask> run_task;
            std::chrono::milliseconds sleep_time{0};
        };

        NextTask GetNextTask();
        void ProcessTasks();
        void NotifyWake();

        std::mutex notify_mutex_;
        std::condition_variable notify_cv_;
        std::atomic<bool> notify_ready_{false};

        std::mutex pending_lock_;
        std::atomic<bool> thread_should_quit_{false};
        std::atomic<OrderId> thread_posting_order_{0};
        std::queue<std::pair<OrderId, std::unique_ptr<QueuedTask>>> pending_queue_;
        std::map<DelayedEntryTimeout, std::unique_ptr<QueuedTask>> delayed_queue_;
        
        std::mutex start_mutex_;
        std::condition_variable start_cv_;
        std::atomic<bool> started_{false};

        std::thread thread_;
        std::string name_;
    };
}

