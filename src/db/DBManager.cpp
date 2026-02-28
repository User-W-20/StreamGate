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
    //基础配置校验
    if (_config.minSize < 0 || _config.maxSize < _config.minSize)
    {
        throw std::invalid_argument("DBManager: Invalid pool size configuration (minSize/maxSize).");
    }

    //获取驱动实例 (MariaDB Connector/C++ 特有)
    try
    {
        _driver = sql::mariadb::get_driver_instance();
    }
    catch (sql::SQLException& e)
    {
        throw std::runtime_error("DBManager: Failed to get MariaDB driver: " + std::string(e.what()));
    }

    //预填充最小连接数 (锁保护)
    std::lock_guard<std::mutex> lock(_mutex);
    for (int i = 0; i < _config.minSize; ++i)
    {
        try
        {
            if (auto conn = createConnection())
            {
                _pool.push(std::move(conn));
                _total_conns.fetch_add(1, std::memory_order_acq_rel);
                markConnectionCreated();
            }
            else
            {
                LOG_ERROR("DBManager: Initial connection creation failed at index " + std::to_string(i));
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("DBManager: Exception during initial pre-fill: " + std::string(e.what()));
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

        //使用 exchange 保证只有第一个到达的线程执行销毁逻辑 (幂等性)
        if (_shutdown.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        LOG_INFO("DBManager: Shutting down connection pool, cleaning up idle connections...");

        //清空池子里的所有闲置连接
        while (!_pool.empty())
        {
            _pool.pop();

            _total_conns.fetch_sub(1, std::memory_order_acq_rel);
            markConnectionDestroyed();
        }
    }

    //锁外通知：瞬间击穿所有正在 wait_until 的 acquire 线程
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
    if (_shutdown.load(std::memory_order_acquire))return nullptr;

    std::unique_lock<std::mutex> lock(_mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_config.checkoutTimeoutMs);

    while (true)
    {
        if (_shutdown.load(std::memory_order_acquire))return nullptr;

        //调试期：验证不变量 (资源总数永远不应小于空闲池)
        assert(_total_conns.load(std::memory_order_relaxed)>=static_cast<int>(_pool.size()));

        // 策略 A: 池化获取 (受 Mutex 保护)
        if (!_pool.empty())
        {
            auto conn = std::move(_pool.front());
            _pool.pop();

            lock.unlock(); // 校验可能耗时，释放锁
            if (validateConnection(conn.get()))
            {
                markConnectionAcquired();
                return conn;
            }

            // 连接失效：相当于池子里少了一个连接
            // 直接用 _total_conns 减 1 即可，无需再调 markConnectionDestroyed 包装函数
            _total_conns.fetch_sub(1, std::memory_order_acq_rel);
            lock.lock();
            continue;
        }

        // 策略 B: 尝试扩容 (高效 Weak CAS 循环)
        int current = _total_conns.load(std::memory_order_relaxed);
        if (current < _config.maxSize)
        {
            if (_total_conns.compare_exchange_weak(
                current, current + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                lock.unlock();
                if (auto conn = createConnection())
                {
                    markConnectionAcquired();
                    return conn;
                }
                _total_conns.fetch_sub(1, std::memory_order_acq_rel);
                lock.lock();
                goto POOL_RECHECK; // 创建失败回退到顶部重新判定
            }
            // CAS 失败时 current 会自动更新，while 循环会继续尝试直到满了或成功
        }

    POOL_RECHECK:
        // 策略 C: 阻塞等待 (RAII + Notify-aware)
        {
            _wait_threads.fetch_add(1, std::memory_order_relaxed);
            struct WaitGuard
            {
                std::atomic<int>& counter;

                ~WaitGuard()
                {
                    counter.fetch_sub(1, std::memory_order_relaxed);
                }
            } guard{_wait_threads};

            if (!_cv.wait_until(lock, deadline, [this]()
            {
                return !_pool.empty() || _shutdown.load(std::memory_order_acquire);
            }))
            {
                LOG_WARN("DBManager: Acquire timeout");
                return nullptr;
            }
        }

        if (_shutdown.load(std::memory_order_acquire)) return nullptr;
        // 唤醒后续继续循环读取
    }
}

void DBManager::releaseConnection(std::unique_ptr<sql::Connection> conn)
{
    if (!conn)return;

    //只要归还，active 计数必然减少
    markConnectionReleased();

    //检查系统状态：如果已经关闭，直接销毁连接并减少 total 总数
    if (_shutdown.load(std::memory_order_acquire))
    {
        _total_conns.fetch_sub(1, std::memory_order_acq_rel);
        markConnectionDestroyed();

        return;
    }

    //正常归还：将连接推回池中 (受锁保护)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pool.push(std::move(conn));
    }

    //锁外通知：避免唤醒后的线程立即产生锁竞争
    _cv.notify_one();
}

bool DBManager::isConnected() const
{
    //如果正在关闭，直接判定为不可用
    if (_shutdown.load(std::memory_order_acquire))
    {
        return false;
    }

    //只要系统总连接数（含池内和借出的）大于 0，即视为逻辑连通
    return _total_conns.load(std::memory_order_acquire) > 0;
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
