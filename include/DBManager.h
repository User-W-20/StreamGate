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
#include <optional>
#include "StreamAuthData.h"
#include "ThreadPool.h"
#include <mariadb/conncpp.hpp>

class DBManager
{
public:
    static DBManager& instance();

    void connect();

    //异步执行数据库检查
    std::future<int> asyncCheckStream(const std::string& streamName,
                                      const std::string& clientId,
                                      const std::string& authToken);

    [[nodiscard]] ThreadPool& getThreadPool() const
    {
        return *_threadPool;
    }

    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;

    bool insertAuthForTest(const std::string& stream,
                           const std::string& client,
                           const std::string& token);

    std::optional<StreamAuthData> getAuthDataFromDB(const std::string& streamKey,
                                                    const std::string& clientId,
                                                    const std::string& authToken);

private:
    DBManager();
    ~DBManager();

private:
    sql::Driver* _dirver;

    std::mutex _connectionMutex;
    std::queue<std::unique_ptr<sql::Connection>> _connectionPool;

     std::unique_ptr<ThreadPool> _threadPool;

    std::unique_ptr<sql::Connection> getConnection();
    void releaseConnection(std::unique_ptr<sql::Connection> conn);

    int performSyncCheck(const std::string& streamName, const std::string& clientId, const std::string& authToken);
};
#endif //STREAMGATE_DBMANAGER_H