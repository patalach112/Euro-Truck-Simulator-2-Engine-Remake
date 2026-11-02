#include "thread_pool.h"

#include <algorithm>
#include <cstdlib>

namespace thread_pool {
namespace {

unsigned resolve_worker_count(unsigned requested) noexcept {
    if (requested) return (std::min)(requested, 64u);
    const unsigned detected = std::thread::hardware_concurrency();
    if (!detected) return 2;
    // One thread is left for the render pipeline; small machines still get a second worker.
    return detected > 2 ? (std::min)(detected - 1, 63u) : detected;
}

// Four chunks per worker keeps the queue short while still balancing uneven item costs.
unsigned chunk_size(unsigned count, unsigned workers) noexcept {
    const unsigned target = workers * 4u;
    return (std::max)(1u, (count + target - 1u) / target);
}

}  // namespace

pool::pool(unsigned worker_count) noexcept {
    const unsigned target = resolve_worker_count(worker_count);
    threads_.reserve(target);
    for (unsigned index = 0; index < target; ++index) {
        try {
            threads_.emplace_back(&pool::run, this);
        } catch (...) {
            break;
        }
    }
    // Published only after every worker exists; workers never read this field.
    workers_ = static_cast<unsigned>(threads_.size());
}

pool::~pool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& thread : threads_) {
        if (thread.joinable()) thread.join();
    }
}

void pool::execute(const job& work) noexcept {
    for (unsigned index = work.begin; index < work.end; ++index) {
        work.fn(work.context, index);
        tasks_.fetch_add(1, std::memory_order_relaxed);
    }
}

void pool::run() noexcept {
    for (;;) {
        job work{};
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            work = queue_.front();
            queue_.pop_front();
        }
        execute(work);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (--pending_ == 0) done_.notify_all();
        }
    }
}

bool pool::dispatch(task_fn fn, void* context, unsigned count) noexcept {
    if (!fn || !count) return true;
    if (!workers_) {
        for (unsigned index = 0; index < count; ++index) fn(context, index);
        tasks_.fetch_add(count, std::memory_order_relaxed);
        return true;
    }

    const unsigned step = chunk_size(count, workers_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        for (unsigned begin = 0; begin < count; begin += step) {
            queue_.push_back({fn, context, begin, (std::min)(count, begin + step)});
            ++pending_;
            chunks_.fetch_add(1, std::memory_order_relaxed);
        }
        dispatches_.fetch_add(1, std::memory_order_relaxed);
    }
    wake_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return pending_ == 0; });
    return true;
}

}  // namespace thread_pool

namespace thread_pool {

pool* shared() noexcept {
    static pool* instance = new pool();
    return instance;
}

}  // namespace thread_pool
