#include "task_queue.hpp"
#include "task_queue_base.hpp"
#include "task_queue_stdlib.hpp"

namespace core {

    TaskQueue::TaskQueue(std::unique_ptr<TaskQueueBase, TaskQueueDeleter> taskQueue)
    : impl_(taskQueue.release()) {}

    TaskQueue::~TaskQueue() {
        impl_->Delete();
    }

    bool TaskQueue::IsCurrent() const {
        return impl_->IsCurrent();
    }

    void TaskQueue::PostTask(std::unique_ptr<QueuedTask> task) {
        return impl_->PostTask(std::move(task));
    }

    void TaskQueue::PostDelayedTask(std::unique_ptr<QueuedTask> task, std::chrono::milliseconds delay) {
        return impl_->PostDelayedTask(std::move(task), delay);
    }

    std::unique_ptr<TaskQueue> TaskQueue::Create(std::string_view name) {
        return std::make_unique<TaskQueue>(std::unique_ptr<TaskQueueBase, TaskQueueDeleter>(new TaskQueueStdlib(name)));
    }

}
