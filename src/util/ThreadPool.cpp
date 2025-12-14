//
// Created by wxx on 12/13/25.
//
#include "ThreadPool.h"
#include <stdexcept>
#include "Logger.h"

ThreadPool::ThreadPool(size_t threads)
{
    if (threads == 0)
    {
        throw std::invalid_argument("ThreadPool size must be greater than zero.");
    }
    LOG_INFO("ThreadPool starting with "+std::to_string(threads)+" workers.");

    for (size_t i = 0; i < threads; ++i)
    {
        workers.emplace_back([this]
        {
            for (;;)
            {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock,
                                         [this]
                                         {
                                             return this->stop.load() || ! this->tasks.empty();
                                         });

                    if (this->stop.load() && this->tasks.empty())
                    {
                        return;
                    }

                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                try
                {
                    task();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("ThreadPool worker caught exception: "+std::string(e.what()));
                }
                catch (...)
                {
                    LOG_ERROR("ThreadPool worker caught unknown exception.");
                }
            }
        });
    }
}

void ThreadPool::stop_and_wait()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }

    condition.notify_all();

    for (std::thread& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    LOG_INFO("ThreadPool successfully stopped.");
}

ThreadPool::~ThreadPool()
{
    if (! stop.load())
    {
        stop_and_wait();
    }
}