#include "gtest/gtest.h"
#include "AuthManager.h"
#include "CacheManager.h"
#include "DBManager.h"
#include "ConfigLoader.h"
#include "HookServer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <stdexcept>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;
using namespace std::chrono_literals;

// ---------------------- 模拟服务控制 ----------------------
void toggle_service(const std::string& service_name, bool start)
{
    std::string cmd = start ? "start" : "stop";
    std::cout << "\n>>> " << (start ? "Starting" : "Stopping") << service_name << "... " << std::flush;

    // 使用 -q (quiet) 避免输出警告，除非失败
    std::string full_cmd = "sudo systemctl " + cmd + " " + service_name + " > /dev/null 2>&1";

    if (std::system(full_cmd.c_str()) != 0)
    {
        full_cmd = "sudo systemctl " + cmd + " " + service_name;
        if (std::system(full_cmd.c_str()) != 0)
        {
            std::cerr << "::Warning: Failed to " << cmd << " " << service_name << ". Continuing." << std::endl;
        }
    }

    // 保持 500ms sleep
    std::this_thread::sleep_for(500ms);
    std::cout << "Done." << std::endl;
}

// ---------------------- 测试夹具 ----------------------
class HookServerIntegrationTest : public ::testing::Test
{
protected:
    static constexpr const char* TEST_ADDRESS = "127.0.0.1";
    static constexpr int TEST_PORT = 9001;
    static constexpr int IO_THREADS = 4;

    static constexpr const char* VALID_STREAM = "test_stream_valid_gtest";
    static constexpr const char* VALID_CLIENT = "client_gtest_001";
    static constexpr const char* VALID_TOKEN = "valid_token_gtest";
    static constexpr const char* INVALID_STREAM = "test_stream_invalid_gtest";

    // 静态成员用于在整个测试套件生命周期内管理服务器
    static StreamGateServer* server_;
    static std::thread server_thread_;
    static std::atomic_bool server_started_;

    static void SetUpTestSuite()
    {
        std::cout << "\n--- GLOBAL SERVICE STARTUP ---" << std::endl;
        toggle_service("mariadb", true);
        toggle_service("redis-server", true);
        std::cout << "--- GLOBAL SERVICE STARTUP COMPLETE ---" << std::endl;
    }

    int send_hook_request(const std::string& stream, const std::string& client, const std::string& token)
    {
        try
        {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            tcp::socket socket(ioc);
            net::connect(socket, resolver.resolve(TEST_ADDRESS, std::to_string(TEST_PORT)));

            std::string body = "{\"streamKey\":\"" + stream +
                               "\",\"clientId\":\"" + client +
                               "\",\"authToken\":\"" + token + "\"}";

            http::request<http::string_body> req{http::verb::post, "/hook", 11};
            req.set(http::field::host, TEST_ADDRESS);
            req.set(http::field::user_agent, "IntegrationTestClient");
            req.set(http::field::content_type, "application/json");
            req.body() = body;
            req.prepare_payload();

            http::write(socket, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(socket, buffer, res);

            return static_cast<int>(res.result_int());
        }
        catch (const std::exception& e)
        {
            std::cerr << "Client error: " << e.what() << std::endl;
            return 503;
        }
    }

    void SetUp() override
    {
        std::cout << "\n--- TEST SETUP ---" << std::endl;

        // 2. 初始化单例和连接
        ConfigLoader::instance().load("config/config.ini", ".env");
        DBManager::instance().connect();

        // 关键点：每次 SetUp 都确保 CacheManager 的 I/O 循环启动 (处理重启逻辑)
        CacheManager::instance().start_io_loop();
        std::this_thread::sleep_for(1s);

        // 3. 插入测试数据 (T01 依赖它)
        DBManager::instance().insertAuthForTest(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);

        // 4. 启动 server (仅启动一次)
        if (! server_started_.load())
        {
            server_ = new StreamGateServer(TEST_ADDRESS, TEST_PORT, IO_THREADS);
            server_thread_ = std::thread([]
            {
                try
                {
                    server_->run();
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Server thread crashed: " << e.what() << std::endl;
                }
            });
            std::this_thread::sleep_for(2s);
            server_started_ = true;
        }
    }

    void TearDown() override
    {
        std::cout << "\n--- TEST TEARDOWN ---" << std::endl;
        // 关键点：确保 Redis 客户端在每次测试后关闭 I/O 线程，防止影响下一个测试
        CacheManager::instance().force_disconnect();
    }

    static void TearDownTestSuite()
    {
        std::cout << "\n--- TEARDOWN TEST SUITE ---" << std::endl;

        if (server_started_.load())
        {
            // 1. 停止服务器对象
            if (server_)
            {
                server_->stop();
            }

            // 2. 等待服务器 I/O 线程安全退出 (join)
            if (server_thread_.joinable())
            {
                server_thread_.join();
            }

            // 3. 释放裸指针内存
            delete server_;
            server_ = nullptr;
            server_started_ = false;
        }

        // 4. 停止外部服务 (只在整个套件结束后执行一次)
        toggle_service("mariadb", false);
        toggle_service("redis-server", false);

        std::cout << "--- TEARDOWN TEST SUITE COMPLETED ---" << std::endl;
    }
};

// ---------------------- 静态成员初始化 ----------------------
StreamGateServer* HookServerIntegrationTest::server_ = nullptr;
std::thread HookServerIntegrationTest::server_thread_;
std::atomic_bool HookServerIntegrationTest::server_started_ = false;

// ---------------------- 测试 ----------------------

TEST_F(HookServerIntegrationTest, T01_NormalFlow_CacheHit)
{
    int status1 = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status1, 200);

    int status2 = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status2, 200);
}

TEST_F(HookServerIntegrationTest, T02_NormalFlow_DBFailure)
{
    int status = send_hook_request(INVALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status, 403);
}

TEST_F(HookServerIntegrationTest, T03_FaultTolerance_RedisDown)
{
    // 1. 强制客户端断开并停止 I/O 线程 (消除 50 秒延迟)
    CacheManager::instance().force_disconnect();

    toggle_service("redis-server", false);

    int status = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status, 200); // 降级到 DB 成功

    toggle_service("redis-server", true);

    // 💥 修复点：等待 2 秒，确保 Redis 端口已监听 💥
    std::this_thread::sleep_for(2000ms);

    // 2. 重新启动 I/O 线程和客户端连接
    CacheManager::instance().reconnect();

    // 增加额外的等待，确保 CacheManager 完成 I/O 线程上的连接建立
    std::this_thread::sleep_for(1000ms);
}

TEST_F(HookServerIntegrationTest, T04_FaultTolerance_DBDown)
{
    toggle_service("mariadb", false);
    int status = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status, 500);
    toggle_service("mariadb", true);
}

TEST_F(HookServerIntegrationTest, T05_FaultTolerance_DoubleFailure)
{
    // 1. 强制客户端断开并停止 I/O 线程
    CacheManager::instance().force_disconnect();
    toggle_service("redis-server", false);

    toggle_service("mariadb", false);

    int status = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status, 500);

    // 恢复服务
    toggle_service("redis-server", true);

    // 💥 修复点：等待 2 秒，确保 Redis 端口已监听 💥
    std::this_thread::sleep_for(2000ms);

    // 2. 重新启动 I/O 线程和客户端连接
    CacheManager::instance().reconnect();

    toggle_service("mariadb", true);
    // 增加额外的等待，确保连接建立
    std::this_thread::sleep_for(1000ms);
}

// ---------------------- 主函数 ----------------------
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}