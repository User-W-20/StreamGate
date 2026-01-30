//
// Created by X on 2025/11/17.
//
#include "DBManager.h"
#include "ConfigLoader.h"
#include "Logger.h"
#include <stdexcept>
#include <chrono>
#include <nlohmann/json.hpp>
#include <utility>

//DBManager 实现
DBManager::DBManager(Config config) : _config(std::move(config))
{
    if (_config.minSize < 0 || _config.maxSize < _config.minSize)
    {
        throw std::invalid_argument("DBManager: Invalid pool size configuration.");
    }

    try
    {
        _driver = sql::mariadb::get_driver_instance();
    }
    catch (sql::SQLException& e)
    {
        throw std::runtime_error("DBManager: Failed to get MariaDB driver: " + std::string(e.what()));
    }

    //预填充最小连接数
    std::lock_guard<std::mutex> lock(_mutex);
    for (int i = 0; i < _config.minSize; ++i)
    {
        if (auto conn = createConnection())
        {
            _pool.push(std::move(conn));
            ++_currentSize;
        }
    }
}

DBManager::~DBManager()
{
    shutdown();
}

void DBManager::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_shutdown.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        LOG_INFO("DBManager: Shutting down connection pool...");

        //清空池子里的所有闲置连接
        size_t idleCount = _pool.size();
        while (!_pool.empty())
        {
            _pool.pop();
        }

        //更新计数（只减去池子里的，借出去的由 releaseConnection 处理）
        _currentSize.fetch_sub(static_cast<int>(idleCount), std::memory_order_release);
    }

    //唤醒所有正在 CV 上等待连接的业务线程
    // 那些线程醒来后检查 _shutdown 为 true，会立即抛出异常或返回空
    _cv.notify_all();
}

std::unique_ptr<sql::Connection> DBManager::createConnection() const
{
    try
    {
        auto* raw_conn = _driver->connect(_config.url, _config.user, _config.password);

        if (!raw_conn)
        {
            LOG_WARN("DBManager: Driver returned null connection for URL: " + _config.url);
            return nullptr;
        }

        LOG_DEBUG("DBManager: Successfully created new connection to " + _config.url);
        return std::unique_ptr<sql::Connection>(raw_conn);
    }
    catch (sql::SQLException& e)
    {
        LOG_ERROR("DBManager: Connect failed (SQL). URL: " + _config.url +
            ", User: " + _config.user + ", Error: " + std::string(e.what()));
        return nullptr;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("DBManager: Connect failed (System). Error: " + std::string(e.what()));
        return nullptr;
    }
    catch (...)
    {
        LOG_ERROR("DBManager: Connect failed with unknown fatal exception.");
        return nullptr;
    }
}

bool DBManager::validateConnection(sql::Connection* conn)
{
    assert(conn!=nullptr);

    try
    {
        const std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        if (!stmt)return false;
        //执行极简心跳查询

        const std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
        return res && res->next();
    }
    catch (sql::SQLException& e)
    {
        // 针对数据库驱动异常的特化处理
        LOG_DEBUG("DBManager: Connection validation failed (SQL Error: " +
            std::to_string(e.getErrorCode()) + ") " + e.what());
        return false;
    }
    catch (const std::exception& e)
    {
        // 捕获其他可能的系统异常或内存异常
        LOG_DEBUG("DBManager: Connection validation failed (System Error): " + std::string(e.what()));
        return false;
    }
    catch (...)
    {
        LOG_ERROR("DBManager: Connection validation failed with an unknown fatal error.");
        return false;
    }
}

std::unique_ptr<sql::Connection> DBManager::acquireConnection()
{
    // 关机预检
    if (_shutdown.load(std::memory_order_acquire))return nullptr;

    std::unique_lock<std::mutex> lock(_mutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_config.checkoutTimeoutMs);

    while (true)
    {
        // 拿锁后二次检查
        if (_shutdown.load(std::memory_order_acquire))return nullptr;

        // 策略 A: 从池中获取
        if (!_pool.empty())
        {
            auto conn = std::move(_pool.front());
            _pool.pop();

            lock.unlock(); // 校验可能耗时，释放锁
            if (validateConnection(conn.get()))
            {
                return conn;
            }

            // 连接失效：减计数并继续寻找下一个
            _currentSize.fetch_sub(1, std::memory_order_release);
            lock.lock();
            continue;
        }

        // 策略 B: 尝试扩容
        int current = _currentSize.load(std::memory_order_acquire);
        if (current < _config.maxSize)
        {
            // 尝试预占位
            if (_currentSize.compare_exchange_strong(current, current + 1))
            {
                lock.unlock();
                if (auto conn = createConnection())
                {
                    return conn;
                }

                // 创建失败：回滚预占位
                _currentSize.fetch_sub(1, std::memory_order_release);
                lock.lock();
            }
        }

        // 策略 C: 阻塞等待
        // 如果无法扩容也拿不到池子里的，就只能等别人归还
        if (_cv.wait_until(lock, deadline, [this]()
        {
            return !_pool.empty() || _shutdown.load(std::memory_order_acquire);
        }))
        {
            // 唤醒后继续循环
            continue;
        }
        else
        {
            // 超时退出
            return nullptr;
        }
    }
}

void DBManager::releaseConnection(std::unique_ptr<sql::Connection> conn)
{
    if (!conn)return;

    // 归还前探活
    if (conn->isClosed() || !validateConnection(conn.get()))
    {
        _currentSize.fetch_sub(1, std::memory_order_release);
        return;
    }

    //状态检查与入池
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_shutdown.load(std::memory_order_acquire))
        {
            _currentSize.fetch_sub(1, std::memory_order_release);
            return;
        }
        _pool.push(std::move(conn));
    }

    _cv.notify_one();
}

bool DBManager::isConnected() const
{
    //如果正在关闭，直接判定为不健康
    if (_shutdown.load(std::memory_order_relaxed))
    {
        return false;
    }

    //只要池内维持有存量连接，即视为逻辑连通
    // 具体的连接有效性由 ConnectionGuard 在 acquire 时处理
    return _currentSize.load(std::memory_order_relaxed) > 0;
}

//ConnectionGuard 实现
ConnectionGuard::ConnectionGuard(DBManager& manager)
    : _manager(&manager), _conn(manager.acquireConnection())
{
}

ConnectionGuard::~ConnectionGuard()
{
    if (_conn && _manager)
    {
        _manager->releaseConnection(std::move(_conn));
    }
}

ConnectionGuard::ConnectionGuard(ConnectionGuard&& other) noexcept
    : _manager(other._manager), _conn(std::move(other._conn))
{
    other._manager = nullptr;
}

ConnectionGuard& ConnectionGuard::operator=(ConnectionGuard&& other) noexcept
{
    if (this != &other)
    {
        _manager = other._manager;
        _conn = std::move(other._conn);
        other._manager = nullptr;
    }

    return *this;
}
