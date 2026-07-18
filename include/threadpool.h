/**
 * @file threadpool.h
 * @brief Fixed-size thread pool with FIFO task queue.
 *
 * Workers are created at construction and persist for the pool's lifetime.
 * Shutdown drains the queue before joining threads, ensuring every
 * submitted task completes.
 */

#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace http {

class ThreadPool {
public:
    /** @brief Construct the pool and start @p num_workers threads (>= 1). */
    explicit ThreadPool(std::size_t num_workers);

    /** @brief Stop the pool and join all workers. */
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Enqueue a task. No-op after stop() has been called.
     * @param task  Any callable matching void().
     */
    void submit(std::function<void()> task);

    /**
     * @brief Signal shutdown and block until all threads exit.
     * Safe to call more than once (idempotent via atomic exchange).
     */
    void stop();

    /** @return Number of tasks waiting in the queue. */
    std::size_t queue_size() const;

    /** @return Total tasks completed since construction. */
    std::size_t tasks_completed() const noexcept { return tasks_completed_.load(); }

private:
    void worker_loop();

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> task_queue_;
    mutable std::mutex                queue_mutex_;
    std::condition_variable           queue_cv_;
    std::atomic<bool>                 stop_{false};
    std::atomic<std::size_t>          tasks_completed_{0};
};

} // namespace http
