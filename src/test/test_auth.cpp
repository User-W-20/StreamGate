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
    std::string full_cmd = "sudo systemctl " + cmd + " " + service_name;
    if (std::system(full_cmd.c_str()) != 0)
        std::cerr << "Warning: Failed to " << cmd << " " << service_name << ". Continuing." << std::endl;
    std::this_thread::sleep_for(2s);
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

    static StreamGateServer* server_;
    static std::thread server_thread_;
    static std::atomic_bool server_started_;

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

        // 启动依赖服务
        toggle_service("mariadb", true);
        toggle_service("redis-server", true);

        ConfigLoader::instance().load("config/config.ini", ".env");
        DBManager::instance().connect();
        CacheManager::instance().start_io_loop();
        std::this_thread::sleep_for(1s);

        // 插入测试数据
        DBManager::instance().insertAuthForTest(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);

        // 启动 server
        if (! server_started_)
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
    }
};

// ---------------------- 静态成员 ----------------------
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
    toggle_service("redis-server", false);
    int status = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status, 200); // 期望服务器能处理 Redis 不可用
    toggle_service("redis-server", true);
}

TEST_F(HookServerIntegrationTest, T04_FaultTolerance_DBDown)
{
    toggle_service("mariadb", false);
    int status = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status, 500); // DB 不可用时返回 500
    toggle_service("mariadb", true);
}

TEST_F(HookServerIntegrationTest, T05_FaultTolerance_DoubleFailure)
{
    toggle_service("redis-server", false);
    toggle_service("mariadb", false);
    int status = send_hook_request(VALID_STREAM, VALID_CLIENT, VALID_TOKEN);
    ASSERT_EQ(status, 500); // 双重故障返回 500
    toggle_service("redis-server", true);
    toggle_service("mariadb", true);
}

// ---------------------- 主函数 ----------------------
int main(int argc, char** argv)
{
    ConfigLoader::instance().load("config/config.ini", ".env");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}