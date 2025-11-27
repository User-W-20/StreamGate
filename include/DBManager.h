//
// Created by X on 2025/11/17.
//

#ifndef STREAMGATE_DBMANAGER_H
#define STREAMGATE_DBMANAGER_H
#include <string>
#include <memory>
#include <future>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <mariadb/conncpp.hpp>

class ThreadPool
{
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();

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

            tasks.emplace([task]()
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
    bool stop;
};

class DBManager
{
public:
    static DBManager& instance();

    void connect();

    //异步执行数据库检查
    std::future<int> asyncCheckStream(const std::string& streamName, const std::string& clientId);

    [[nodiscard]] ThreadPool& getThreadPool()const
    {
        return *_threadPool;
    }

    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;

private:
    DBManager();
    ~DBManager() = default;

private:
    sql::Driver* _dirver;

    std::mutex _connectionMutex;
    std::queue<std::unique_ptr<sql::Connection>> _connectionPool;

    std::unique_ptr<ThreadPool> _threadPool;

    std::unique_ptr<sql::Connection> getConnection();
    void releaseConnection(std::unique_ptr<sql::Connection> conn);

    int performSyncCheck(const std::string& streamName, const std::string& clientId);
};
#endif //STREAMGATE_DBMANAGER_H