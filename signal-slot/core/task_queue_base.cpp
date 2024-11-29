#include "task_queue_base.hpp"
#include <thread>

namespace core {

    namespace {

        thread_local TaskQueueBase* _current = nullptr;

    }  // namespace

    TaskQueueBase* TaskQueueBase::Current() {
        return _current;
    }

    TaskQueueBase::CurrentTaskQueueSetter::CurrentTaskQueueSetter(TaskQueueBase* taskQueue)
    : _previous(_current) {
        _current = taskQueue;
    }

    TaskQueueBase::CurrentTaskQueueSetter::~CurrentTaskQueueSetter() {
        _current = _previous;
    }

}
