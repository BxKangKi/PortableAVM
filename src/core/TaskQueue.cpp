#include "core/TaskQueue.h"

namespace pavm {

TaskQueue::TaskQueue() : worker_([this] { run(); }) {}
TaskQueue::~TaskQueue() { stop(); }

void TaskQueue::post(Task task) {
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
}

bool TaskQueue::busy() const {
    std::scoped_lock lock(mutex_);
    return busy_ || !tasks_.empty();
}

void TaskQueue::stop() {
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TaskQueue::run() {
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
            busy_ = true;
        }
        try {
            task();
        } catch (...) {
            // The application wraps tasks and records errors. Keep the worker alive.
        }
        {
            std::scoped_lock lock(mutex_);
            busy_ = false;
        }
    }
}

} // namespace pavm
