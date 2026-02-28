//
// Created by X on 2025/11/17.
//

#ifndef STREAMGATE_DBMANAGER_H
#define STREAMGATE_DBMANAGER_H
#include <string>
#include <memory>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "StreamAuthData.h"
#include <mariadb/conncpp.hpp>

class DBManager;

struct PoolStats
{
    bool is_ok = false;
    int current_size = 0;
    int active_count = 0;
    int idle_count = 0;
    int wait_count = 0;
};

/**
 * @brief RAII 辅助类：自动管理连接的借出与归还
 */
class ConnectionGuard
{
public:
    explicit ConnectionGuard(DBManager& manager);
    ~ConnectionGuard();

    // 禁用拷贝，允许移动
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
    ConnectionGuard(ConnectionGuard&& other) noexcept;
    ConnectionGuard& operator=(ConnectionGuard&& other) noexcept;

    sql::Connection* operator->() const
    {
        if (!_conn) throw std::runtime_error("DBManager: Accessing null connection via Guard");
        return _conn.get();
    }

    [[nodiscard]] sql::Connection* get() const
    {
        return _conn.get();
    }

    explicit operator bool() const
    {
        return _conn != nullptr;
    }

private:
    DBManager* _manager;
    std::unique_ptr<sql::Connection> _conn;
};

/**
 * @brief MariaDB 连接池管理器
 */
class DBManager
{
public:
    struct Config
    {
        std::string url;
        std::string user;
        std::string password;
        int minSize = 5;
        int maxSize = 20;
        int checkoutTimeoutMs = 5000; // 获取连接的最大等待时间
    };

    explicit DBManager(Config config);
    ~DBManager();

    void shutdown();

    // 核心接口
    std::unique_ptr<sql::Connection> acquireConnection();
    void releaseConnection(std::unique_ptr<sql::Connection> conn);

    // 状态查询
    int getCurrentSize() const
    {
        // return _currentSize.load();
        return _total_conns.load(std::memory_order_acquire);
    }

    bool isShutdown() const
    {
        return _shutdown.load();
    }

    /**
     * @brief 检查连接池逻辑健康状态
     * @note 非阻塞操作，不执行 SQL 探活
     */
    bool isConnected() const;

    /**
     * @brief 获取连接池统计快照
     */
    PoolStats getPoolStats() const noexcept
    {
        PoolStats stats;
        stats.is_ok = this->isConnected();

        // 核心原子读取
        const int total = _total_conns.load(std::memory_order_relaxed);
        const int active = _active_conns.load(std::memory_order_relaxed);

        stats.current_size = total;
        stats.active_count = active;

        // 计算得出 idle，即使发生瞬时漂移，也不会出现逻辑上的“幽灵连接”
        stats.idle_count = (total > active) ? (total - active) : 0;
        stats.wait_count = _wait_threads.load(std::memory_order_relaxed);

        return stats;
    }

protected:
    // 连接取出：只有 active 增加
    void markConnectionAcquired() noexcept
    {
        _active_conns.fetch_add(1, std::memory_order_relaxed);
        assert(_active_conns.load()<=_total_conns.load());
    }

    // 连接归还：只有 active 减少
    void markConnectionReleased() noexcept
    {
        _active_conns.fetch_sub(1, std::memory_order_relaxed);
        assert(_active_conns.load()>=0);
    }

    // 动态扩容：只有 total 增加
    void markConnectionCreated() noexcept
    {
        _total_conns.fetch_add(1, std::memory_order_relaxed);
    }

    // 动态缩容/销毁：只有 total 减少
    void markConnectionDestroyed() noexcept
    {
        _total_conns.fetch_sub(1, std::memory_order_relaxed);
        assert(_total_conns.load()>=_active_conns.load());
    }

private:
    std::unique_ptr<sql::Connection> createConnection() const;
    static bool validateConnection(sql::Connection* conn);

    Config _config;
    sql::Driver* _driver;

    std::queue<std::unique_ptr<sql::Connection>> _pool;
    mutable std::mutex _mutex;
    std::condition_variable _cv;

    // std::atomic<int> _currentSize{0};
    std::atomic<bool> _shutdown{false};

    std::atomic<int> _total_conns{0};
    std::atomic<int> _active_conns{0};
    std::atomic<int> _wait_threads{0};
};
#endif //STREAMGATE_DBMANAGER_H
