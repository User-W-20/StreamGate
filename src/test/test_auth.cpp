#include "gtest/gtest.h"
#include "AuthManager.h"
#include "CacheManager.h"
#include "DBManager.h"
#include "ConfigLoader.h"
#include "HookServer.h"
#include "HybridAuthRepository.h"
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

    // 所有共享资源都是 static + unique_ptr
    static std::unique_ptr<StreamGateServer> server_;
    static std::unique_ptr<HybridAuthRepository> repository_;
    static std::unique_ptr<AuthManager> authManager_;
    static std::atomic<bool> server_started_;

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

        ConfigLoader::instance().load("config/config.ini", ".env");
        DBManager& dbMgr = DBManager::instance();
        CacheManager& cacheMgr = CacheManager::instance();

        dbMgr.connect();
        cacheMgr.start_io_loop();
        std::this_thread::sleep_for(1s);

        dbMgr.insertAuthForTest(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);

        // 只启动一次服务器
        if (!server_started_.load(std::memory_order_acquire))
        {
            ThreadPool& sharedPool = dbMgr.getThreadPool();

            repository_ = std::make_unique<HybridAuthRepository>(dbMgr, cacheMgr);
            authManager_ = std::make_unique<AuthManager>(std::move(repository_), sharedPool);

            server_ = std::make_unique<StreamGateServer>(TEST_ADDRESS, TEST_PORT, IO_THREADS, *authManager_);

            server_->start_service(IO_THREADS);

            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            server_started_.store(true, std::memory_order_release);
        }
    }

    void TearDown() override
    {
        std::cout << "\n--- TEST TEARDOWN ---" << std::endl;
        CacheManager::instance().force_disconnect();
    }

    static void TearDownTestSuite()
    {
        std::cout << "\n--- TEARDOWN TEST SUITE ---" << std::endl;

        if (server_started_.load(std::memory_order_acquire))
        {
            if (server_)
            {
                server_->stop();      // 停止 acceptor + ioc_.stop() + join 所有 I/O threads
                server_.reset();      // 自动释放内存
            }

            try
            {
                DBManager::instance().getThreadPool().stop_and_wait();
            }
            catch (const std::exception& e)
            {
                std::cerr << "Warning: Failed to stop ThreadPool: " << e.what() << std::endl;
            }

            authManager_.reset();
            repository_.reset();

            server_started_.store(false, std::memory_order_release);
        }

        toggle_service("mariadb", false);
        toggle_service("redis-server", false);

        std::cout << "--- TEARDOWN TEST SUITE COMPLETED ---" << std::endl;
    }
};

// ---------------------- 静态成员定义（放在 cpp 文件中） ----------------------
std::unique_ptr<StreamGateServer> HookServerIntegrationTest::server_;
std::unique_ptr<HybridAuthRepository> HookServerIntegrationTest::repository_;
std::unique_ptr<AuthManager> HookServerIntegrationTest::authManager_;
std::atomic<bool> HookServerIntegrationTest::server_started_{false};

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