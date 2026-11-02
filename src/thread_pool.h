#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace thread_pool {

// Per-item entry point. The context is the value handed to dispatch; the index identifies the
// element inside the dispatched range.
using task_fn = void (*)(void* context, unsigned index);

class pool {
public:
    // A count of zero selects std::thread::hardware_concurrency, clamped to the safe range.
    explicit pool(unsigned worker_count = 0) noexcept;
    ~pool();

    pool(const pool&) = delete;
    pool& operator=(const pool&) = delete;

    // Splits [0, count) into chunks and blocks until every chunk has completed.
    bool dispatch(task_fn fn, void* context, unsigned count) noexcept;

    unsigned workers() const noexcept { return workers_; }
    std::uint64_t tasks() const noexcept { return tasks_.load(std::memory_order_relaxed); }
    std::uint64_t dispatches() const noexcept { return dispatches_.load(std::memory_order_relaxed); }
    std::uint64_t chunks() const noexcept { return chunks_.load(std::memory_order_relaxed); }

private:
    struct job {
        task_fn fn;
        void* context;
        unsigned begin;
        unsigned end;
    };

    void run() noexcept;
    void execute(const job& work) noexcept;

    std::vector<std::thread> threads_;
    std::deque<job> queue_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    unsigned workers_ = 0;
    unsigned pending_ = 0;
    bool stopping_ = false;
    std::atomic<std::uint64_t> tasks_{0};
    std::atomic<std::uint64_t> dispatches_{0};
    std::atomic<std::uint64_t> chunks_{0};
};

}  // namespace thread_pool
