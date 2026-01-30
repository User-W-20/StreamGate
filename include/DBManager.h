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
        return _currentSize.load();
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

private:
    std::unique_ptr<sql::Connection> createConnection() const;
    static bool validateConnection(sql::Connection* conn);

    Config _config;
    sql::Driver* _driver;

    std::queue<std::unique_ptr<sql::Connection>> _pool;
    mutable std::mutex _mutex;
    std::condition_variable _cv;

    std::atomic<int> _currentSize{0};
    std::atomic<bool> _shutdown{false};
};
#endif //STREAMGATE_DBMANAGER_H
