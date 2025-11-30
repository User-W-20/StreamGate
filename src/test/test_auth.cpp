// test/hook_server_integration_test.cpp

#include "gtest/gtest.h"
#include "AuthManager.h"
#include "CacheManager.h"
#include "DBManager.h"
#include "ConfigLoader.h"
#include <iostream>
#include <cstdlib>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// 辅助函数：模拟启动/关闭服务 (需要 sudo 权限运行测试)
void toggle_service(const std::string& service_name, bool start)
{
    std::string command = start ? "start" : "stop";
    std::string full_command = "sudo systemctl " + command + " " + service_name;

    std::cout << "\n>>> " << (start ? "Starting" : "Stopping") << " " << service_name << "... " << std::flush;
    if (std::system(full_command.c_str()) != 0)
    {
        std::cerr << "Warning: Failed to " << command << " " << service_name << ". Assuming target state reached." <<
            std::endl;
    }
    std::this_thread::sleep_for(2s); // 等待服务状态改变
    std::cout << "Done." << std::endl;
}

// 定义测试夹具
class AuthManagerTest : public ::testing::Test
{
protected:
    // 模拟成功/失败的参数 (假设这些数据在你的 DB 中存在或不存在)
    const std::string VALID_STREAM = "test_stream_valid_gtest";
    const std::string VALID_CLIENT = "client_gtest_001";
    const std::string VALID_TOKEN = "valid_token_gtest";

    const std::string INVALID_STREAM = "test_stream_invalid_gtest";

    // 模拟 Hook 请求体生成
    std::string create_hook_body(const std::string& s, const std::string& c, const std::string& t)
    {
        return "{\"streamKey\":\"" + s + "\", \"clientId\":\"" + c + "\", \"authToken\":\"" + t + "\"}";
    }

    // 在每个测试开始前运行
    void SetUp() override
    {
        // 确保所有服务都已启动
        std::cout << "\n--- TEST SETUP: Ensuring services are ON and connected ---" << std::endl;
        toggle_service("mariadb", true);
        toggle_service("redis-server", true);

        // 强制初始化单例组件
        // 注意：在实际项目中，你可能需要在组件中实现 reconnect() 来确保连接是活性的
        AuthManager::instance();

        DBManager::instance().connect();
        CacheManager::instance().start_io_loop();

        // 暂时等待，让连接有机会建立
        std::this_thread::sleep_for(3s);
    }

    // 在每个测试结束后运行
    void TearDown() override
    {
        // 测试结束后，恢复服务到运行状态
        std::cout << "\n--- TEST TEARDOWN: Restoring services ---" << std::endl;
        toggle_service("mariadb", true);
        toggle_service("redis-server", true);
    }
};

// --------------------------------------------------------------------------------
// 正常流程测试 (验证缓存逻辑)
// --------------------------------------------------------------------------------

TEST_F(AuthManagerTest, T01_NormalFlow_CacheHit)
{
    // 第一次请求：Cache MISS -> DB Success -> Cache SET
    std::cout << "  [T01] Running 1st query (MISS)..." << std::endl;
    int result1 = AuthManager::checkHook(create_hook_body(VALID_STREAM, VALID_CLIENT, VALID_TOKEN));
    ASSERT_EQ(result1, 200) << "First request (DB) should succeed.";

    // 第二次请求：Cache HIT -> Cache Success
    std::cout << "  [T01] Running 2nd query (HIT)..." << std::endl;
    int result2 = AuthManager::checkHook(create_hook_body(VALID_STREAM, VALID_CLIENT, VALID_TOKEN));
    ASSERT_EQ(result2, 200) << "Second request (Cache) should succeed.";
}

TEST_F(AuthManagerTest, T02_NormalFlow_DBFailure)
{
    // 验证 DB 认证失败 (Cache MISS -> DB Failure -> 403)
    std::cout << "  [T02] Running query for invalid stream..." << std::endl;
    int result = AuthManager::checkHook(create_hook_body(INVALID_STREAM, VALID_CLIENT, VALID_TOKEN));
    ASSERT_EQ(result, 403) << "Request for invalid stream must return 403.";
}

// --------------------------------------------------------------------------------
// 容错测试 - 故障注入
// --------------------------------------------------------------------------------

TEST_F(AuthManagerTest, T03_FaultTolerance_RedisDown)
{
    // 1. 停止 Redis
    toggle_service("redis-server", false);

    // 2. 执行请求 (预期：Cache ERROR -> DB Success -> 200 OK)
    const std::string REDIS_DOWN_STREAM = "redis_down_stream";
    std::cout << "  [T03] Running query with Redis OFF..." << std::endl;

    // 关键验证：服务器不能崩溃 (如果崩溃，测试框架会捕获)
    int result = AuthManager::checkHook(create_hook_body(REDIS_DOWN_STREAM, VALID_CLIENT, VALID_TOKEN));

    // 验证服务器成功回退到 DB
    ASSERT_EQ(result, 200) << "Server must successfully fall back to DB when Redis is down.";
}

TEST_F(AuthManagerTest, T04_FaultTolerance_DBDown)
{
    // 1. 停止 MariaDB
    toggle_service("mariadb", false);

    // 2. 执行请求 (预期：Cache MISS -> DB ERROR -> Fail Closed -> 403 Forbidden)
    const std::string DB_DOWN_STREAM = "db_down_stream";
    std::cout << "  [T04] Running query with DB OFF..." << std::endl;

    // 关键验证：服务器不能崩溃
    int result = AuthManager::checkHook(create_hook_body(DB_DOWN_STREAM, VALID_CLIENT, VALID_TOKEN));

    // 验证服务器执行 Fail Closed 策略
    ASSERT_EQ(result, 403) << "Server must Fail Closed (403) when DB is down.";
}

TEST_F(AuthManagerTest, T05_FaultTolerance_DoubleFailure)
{
    // 1. 确保两者都关闭 (DB在T04中关闭，这里关闭Redis)
    toggle_service("redis-server", false);
    toggle_service("mariadb", false);

    // 2. 执行请求 (预期：Cache ERROR -> DB ERROR -> Fail Closed -> 403 Forbidden)
    const std::string FATAL_STREAM = "fatal_stream";
    std::cout << "  [T05] Running query with BOTH OFF (Testing SegFault fix)..." << std::endl;

    // 关键验证：这是我们之前遇到 Segment Fault 的地方。如果测试通过，说明你的修复成功。
    int result = AuthManager::checkHook(create_hook_body(FATAL_STREAM, VALID_CLIENT, VALID_TOKEN));

    ASSERT_EQ(result, 403) << "Server must Fail Closed (403) when both Cache and DB are down.";
}

// ---
// 主函数
// ---

int main(int argc, char** argv)
{
    // 确保配置加载
    ConfigLoader::instance().load("config/config.ini");

    // 初始化 GTest
    ::testing::InitGoogleTest(&argc, argv);

    // 运行测试，注意必须以 sudo 权限运行
    return RUN_ALL_TESTS();
}