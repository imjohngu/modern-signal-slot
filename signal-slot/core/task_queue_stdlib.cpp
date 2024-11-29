#include "task_queue_stdlib.hpp"
#include <assert.h>

namespace core {

    TaskQueueStdlib::TaskQueueStdlib(std::string_view queue_name)
    : name_(queue_name) {
        thread_ = std::thread([this]{
            CurrentTaskQueueSetter setCurrent(this);
            
            {
                std::lock_guard<std::mutex> lock(start_mutex_);
                started_ = true;
                start_cv_.notify_one();
            }
            
            this->ProcessTasks();
        });

        std::unique_lock<std::mutex> lock(start_mutex_);
        start_cv_.wait(lock, [this]{ return started_.load(); });
    }

    TaskQueueStdlib::~TaskQueueStdlib() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void TaskQueueStdlib::Delete() {
        assert(!IsCurrent());

        {
            std::unique_lock<std::mutex> lock(pending_lock_);
            thread_should_quit_ = true;
        }

        NotifyWake();

        delete this;
    }

    void TaskQueueStdlib::PostTask(std::unique_ptr<QueuedTask> task) {
        {
            std::unique_lock<std::mutex> lock(pending_lock_);
            pending_queue_.push(std::make_pair(++thread_posting_order_, std::move(task)));
        }

        NotifyWake();
    }

    void TaskQueueStdlib::PostDelayedTask(std::unique_ptr<QueuedTask> task, std::chrono::milliseconds delay) {
        DelayedEntryTimeout delayed_entry;
        delayed_entry.next_fire_at = std::chrono::steady_clock::now() + delay;

        {
            std::unique_lock<std::mutex> lock(pending_lock_);
            delayed_entry.order = ++thread_posting_order_;
            delayed_queue_[delayed_entry] = std::move(task);
        }

        NotifyWake();
    }

    void TaskQueueStdlib::PostDelayedHighPrecisionTask(std::unique_ptr<QueuedTask> task, std::chrono::milliseconds delay) {
        PostDelayedTask(std::move(task), delay);
    }

    const std::string& TaskQueueStdlib::Name() const {
        return name_;
    }

    TaskQueueStdlib::NextTask TaskQueueStdlib::GetNextTask() {
        NextTask result;
        
        auto now = std::chrono::steady_clock::now();

        std::unique_lock<std::mutex> lock(pending_lock_);

        if (thread_should_quit_) {
            result.final_task = true;
            return result;
        }

        if (delayed_queue_.size() > 0) {
            auto delayed_entry = delayed_queue_.begin();
            const auto& delay_info = delayed_entry->first;
            auto& delay_run = delayed_entry->second;
            
            if (now >= delay_info.next_fire_at) {
                if (pending_queue_.size() > 0) {
                    auto& entry = pending_queue_.front();
                    auto& entry_order = entry.first;
                    auto& entry_run = entry.second;
                    if (entry_order < delay_info.order) {
                        result.run_task = std::move(entry_run);
                        pending_queue_.pop();
                        return result;
                    }
                }

                result.run_task = std::move(delay_run);
                delayed_queue_.erase(delayed_entry);
                return result;
            }

            result.sleep_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                delay_info.next_fire_at - now);
        }

        if (pending_queue_.size() > 0) {
            auto& entry = pending_queue_.front();
            result.run_task = std::move(entry.second);
            pending_queue_.pop();
        }

        return result;
    }

    void TaskQueueStdlib::ProcessTasks() {
        while (true) {
            auto task = GetNextTask();

            if (task.final_task)
                break;

            if (task.run_task) {
                QueuedTask* release_ptr = task.run_task.release();
                if (release_ptr->run()) {
                    delete release_ptr;
                }
                continue;
            }

            if (task.sleep_time.count() > 0) {
                std::unique_lock<std::mutex> lock(notify_mutex_);
                notify_cv_.wait_for(lock, task.sleep_time, 
                    [this]{ return notify_ready_.load(); });
                notify_ready_ = false;
            }
        }
    }

    void TaskQueueStdlib::NotifyWake() {
        {
            std::lock_guard<std::mutex> lock(notify_mutex_);
            notify_ready_ = true;
        }
        notify_cv_.notify_one();
    }
}
