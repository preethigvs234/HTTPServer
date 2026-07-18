// Fixed-size thread pool.
//
// Workers drain the queue after stop() is called so every accepted
// connection is fully served before the process exits.
// stop() uses exchange() to guard against double-call from both an explicit
// Server::stop() and the ThreadPool destructor.

#include "threadpool.h"
#include <stdexcept>

namespace http {

ThreadPool::ThreadPool(std::size_t num_workers) {
    if (num_workers == 0)
        throw std::invalid_argument("ThreadPool: num_workers must be >= 1");
    workers_.reserve(num_workers);
    for (std::size_t i = 0; i < num_workers; ++i)
        workers_.emplace_back(&ThreadPool::worker_loop, this);
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return;   // reject new work after shutdown
        task_queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
}

void ThreadPool::stop() {
    if (stop_.exchange(true)) return;   // idempotent
    queue_cv_.notify_all();
    for (auto& t : workers_)
        if (t.joinable()) t.join();
}

std::size_t ThreadPool::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || stop_.load();
            });
            if (stop_.load() && task_queue_.empty()) return;
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        task();
        ++tasks_completed_;
    }
}

} // namespace http
