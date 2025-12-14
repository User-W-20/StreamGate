//
// Created by wxx on 12/13/25.
//

#ifndef STREAMGATE_THREADPOOL_H
#define STREAMGATE_THREADPOOL_H
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <thread>
#include <atomic>

class ThreadPool
{
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();

    void stop_and_wait();

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop)
                throw std::runtime_error("submit on stopped ThreadPool");

            tasks.emplace([task]
            {
                (*task)();
            });
        }
        condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;

    std::atomic<bool> stop{false};
};
#endif //STREAMGATE_THREADPOOL_H