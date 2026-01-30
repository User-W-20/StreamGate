//
// Created by wxx on 12/13/25.
//

#ifndef STREAMGATE_THREADPOOL_H
#define STREAMGATE_THREADPOOL_H
#include <future>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <atomic>
#include <stop_token>

#include "Logger.h"

class ThreadPool
{
public:
    struct Config
    {
        size_t num_threads{};
        size_t max_queue_size = 1000;
        bool log_exceptions = true;
    };

    struct Stats
    {
        size_t num_threads;
        size_t queued_tasks; // 当前队列堆积数
        uint64_t total_submitted; // 累计提交成功数
        uint64_t completed_tasks; // 累计执行成功数 (无异常)
        uint64_t failed_tasks; // 累计执行失败数 (捕获异常)
        uint64_t rejected_tasks; // 累计被拒绝数 (满额或关闭)
    };

    explicit ThreadPool(size_t threads);
    explicit ThreadPool(const Config& config);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 提交任务到线程池
     * @throw std::runtime_error 如果池子已关闭或队列已满
     */
    template <class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [f=std::forward<F>(f),...args=std::forward<Args>(args)]()mutable
            {
                return std::invoke(std::move(f), std::move(args)...);
            }
        );

        std::future<return_type> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(_queueMutex);

            //严格的状态判定：关闭后拒绝所有新任务
            if (_stop.load(std::memory_order_relaxed))[[unlikely]]
            {
                _rejectedTasks.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("ThreadPool is stopping or closed");
            }

            //队列容量保护
            if (_maxQueueSize > 0 && _tasks.size() >= _maxQueueSize)
            {
                _rejectedTasks.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("ThreadPool queue is full (" + std::to_string(_maxQueueSize) + ")");
            }

            _tasks.emplace([task]()
            {
                (*task)();
            });

            _totalSubmitted.fetch_add(1, std::memory_order_relaxed);

            if (_tasks.size() > _maxQueueSize / 2)[[unlikely]]
            {
                checkQueueSize(_tasks.size());
            }
        }

        _condition.notify_one();
        return result;
    }

    void stop_and_wait(std::chrono::milliseconds timeout = std::chrono::milliseconds::zero());

    bool is_stopped() const
    {
        return _stop.load(std::memory_order_relaxed);
    }

    Stats get_stats() const;
    void reset_stats();

private:
    void worker_thread(std::stop_token stoken);
    void checkQueueSize(size_t size);

    size_t _numThreads;
    size_t _maxQueueSize;
    bool _logExceptions;

    std::vector<std::jthread> _workers;
    std::queue<std::function<void()>> _tasks;

    mutable std::mutex _queueMutex;
    std::condition_variable_any _condition;
    std::atomic<bool> _stop{false};

    std::atomic<uint64_t> _totalSubmitted{0};
    std::atomic<uint64_t> _completedTasks{0};
    std::atomic<uint64_t> _failedTasks{0};
    std::atomic<uint64_t> _rejectedTasks{0};
    std::atomic<size_t> _lastLoggedSize{0};
};
#endif //STREAMGATE_THREADPOOL_H
