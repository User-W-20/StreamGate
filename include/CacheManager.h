//
// Created by X on 2025/11/22.
//

#ifndef STREAMGATE_CACHEMANAGER_H
#define STREAMGATE_CACHEMANAGER_H
#include <memory>
#include <string>
#include <future>
#include <boost/asio/io_context.hpp>
#include <cpp_redis/core/client.hpp>

#include "HookServer.h"

enum CacheResult
{
    CACHE_HIT_SUCCESS = 1,
    CACHE_HIT_FAILURE = 2,
    CACHE_MISS = -1,
    CACHE_ERROR = -2
};

class CacheManager
{
public:
    static CacheManager& instance();

    std::future<int> getAuthResult(const std::string& streamKey, const std::string& clientId);
    std::future<void> setAuthResult(const std::string& streamKey, const std::string& clientId, int result);

    std::future<int> getAuthResult(const std::string& cacheKey);
    void setAuthResult(const std::string& cacheKey, int result);

    void start_io_loop();

    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    void force_disconnect();

    void reconnect();

private:
    CacheManager();
    ~CacheManager();

    std::unique_ptr<cpp_redis::client> _client;

    int _cacheTTL = 300;

    [[nodiscard]] static std::string buildKey(const std::string& streamKey, const std::string& clientId);

    bool _is_initialized=false;
    bool _io_threads_running=false;

    [[nodiscard]] bool is_ready() const;

    int performSyncGet(const std::string& key);
    void performSyncSet(const std::string& key, int result);

    boost::asio::io_context _io_context;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> _work_guard;
    std::vector<std::thread> _io_threads;
};
#endif  // STREAMGATE_CACHEMANAGER_H