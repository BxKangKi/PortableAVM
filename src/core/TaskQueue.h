#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace pavm {

class TaskQueue {
public:
    using Task = std::function<void()>;

    TaskQueue();
    ~TaskQueue();
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    void post(Task task);
    [[nodiscard]] bool busy() const;
    void stop();

private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<Task> tasks_;
    bool stopping_ = false;
    bool busy_ = false;
    std::thread worker_;
};

} // namespace pavm
