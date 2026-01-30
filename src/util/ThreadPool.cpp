//
// Created by wxx on 12/13/25.
//
#include "ThreadPool.h"

#include <format>
#include <stdexcept>
#include "Logger.h"

ThreadPool::ThreadPool(size_t threads)
    : ThreadPool(Config{threads, 1000, true})
{
}

ThreadPool::ThreadPool(const Config& config)
    : _numThreads(config.num_threads),
      _maxQueueSize(config.max_queue_size),
      _logExceptions(config.log_exceptions)
{
    if (_numThreads == 0)
        throw std::invalid_argument("Threads must > 0");

    LOG_INFO("[ThreadPool] Initialized with " + std::to_string(_numThreads) + " workers");

    _workers.reserve(_numThreads);
    for (size_t i = 0; i < _numThreads; ++i)
    {
        _workers.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool()
{
    stop_and_wait();
}

/**
 * @brief 工作线程主循环 (Drain 模式实现)
 * @param stoken C++20 自动提供的停止令牌
 */
void ThreadPool::worker_thread(std::stop_token stoken)// NOLINT(performance-unnecessary-value-param)
{
    // [IMPORTANT] 退出由「队列为空」驱动。
    // stoken 仅用于唤醒 wait 和作为「准许退出」的信号，严禁用于中断尚未处理的任务。
    while (true)
    {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(_queueMutex);

            // 核心等待逻辑 (C++20 condition_variable_any)
            // 当 request_stop() 被调用时，stoken 会变为 signaled 状态，
            // 此时 wait 会被立刻唤醒并返回 false（即使没有 notify_all）。
            _condition.wait(lock, stoken, [this]
            {
                return !_tasks.empty();
            });

            // 退出判定逻辑 (满足 Drain 语义)
            if (_tasks.empty())
            {
                // 只有当：外部请求停止(stoken) 且 任务全部处理完(empty) 时，才允许 return
                if (stoken.stop_requested())[[unlikely]]
                {
                    return;
                }

                // 如果任务队列仍为空（虚假唤醒），继续等待
                continue;
            }

            //提取任务
            task = std::move(_tasks.front());
            _tasks.pop();
        }
        //执行任务 (双重异常防御)
        try
        {
            if (task)[[likely]]
            {
                task();
                _completedTasks.fetch_add(1, std::memory_order_relaxed);
            }
        }
        catch (const std::exception& e)
        {
            _failedTasks.fetch_add(1, std::memory_order_relaxed);
            if (_logExceptions)[[unlikely]]
            {
                LOG_ERROR("[ThreadPool] Task exception: " + std::string(e.what()));
            }
        }
        catch (...)
        {
            _failedTasks.fetch_add(1, std::memory_order_relaxed);
            if (_logExceptions)[[unlikely]]
            {
                LOG_ERROR("[ThreadPool] Task unknown exception");
            }
        }
    }
}

/**
 * @brief 优雅停机：确保所有已提交任务处理完毕或直到超时
 * @param timeout 等待的最长时间。若为 0，则无限等待直至任务排空。
 */
void ThreadPool::stop_and_wait(std::chrono::milliseconds timeout)
{
    //原子标记关闸，防止重复调用且拒绝后续 submit
    if (_stop.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }

    //发送协作式停止信号并唤醒所有阻塞在 wait 上的线程
    for (auto& worker : _workers)
    {
        worker.request_stop();
    }

    _condition.notify_all();

    //锁定任务锚点：确定停机瞬间的总任务数
    const size_t target = _totalSubmitted.load(std::memory_order_acquire);

    // 执行 Drain（排空）逻辑
    if (timeout > std::chrono::microseconds::zero())
    {
        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            //计算当前已完成（成功+失败）的总数
            size_t finished = _completedTasks.load(std::memory_order_relaxed) + _failedTasks.load(
                std::memory_order_relaxed);

            // 判定 A：任务是否已全部处理完
            if (finished >= target)[[likely]]
            {
                break;
            }

            // 判定 B：是否达到超时时限
            auto elapse = std::chrono::steady_clock::now() - start;
            if (elapse >= timeout)[[unlikely]]
            {
                LOG_FATAL(std::format("[ThreadPool] Drain 超时！仍有 {} 个任务未处理。强制进入清理流程。", target - finished));
                break;
            }
            //短暂让出 CPU，给工作线程时间赶进度
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    else
    {
        LOG_INFO("[ThreadPool] 正在等待所有任务排空 (Infinite Wait)...");
    }

    //物理清理：RAII 核心动作
    // clear 会销毁 vector 中的 jthread 对象，从而触发它们的析构函数
    // 析构函数会阻塞 join，直到 worker_thread 真正 return
    _workers.clear();

    std::string statsMsg = std::format(
        "[ThreadPool] 优雅停机成功. 最终战报: 成功={} 失败={} 拒绝={}",
        _completedTasks.load(std::memory_order_relaxed),
        _failedTasks.load(std::memory_order_relaxed),
        _rejectedTasks.load(std::memory_order_relaxed)
    );

    LOG_INFO(statsMsg);
}

ThreadPool::Stats ThreadPool::get_stats() const
{
    std::lock_guard<std::mutex> lock(_queueMutex);

    return Stats{
        _numThreads,
        _tasks.size(),
        _totalSubmitted.load(),
        _completedTasks.load(),
        _failedTasks.load(),
        _rejectedTasks.load()
    };
}

void ThreadPool::reset_stats()
{
    _totalSubmitted = 0;
    _completedTasks = 0;
    _failedTasks = 0;
    _rejectedTasks = 0;
}

void ThreadPool::checkQueueSize(size_t size)
{
    size_t threshold = _maxQueueSize > 0 ? _maxQueueSize * 3 / 4 : 500;
    if (size >= threshold && size > _lastLoggedSize + 50)
    {
        LOG_WARN("[ThreadPool] High queue depth detected: " + std::to_string(size));
        _lastLoggedSize = size;
    }
}
