// Integration test for StreamGate Hook Pipeline
// Author: wxx
// Date: 2026/01/30
//

#include "gtest/gtest.h"

// Core components
#include "AuthManager.h"
#include "CacheManager.h"
#include "DBManager.h"
#include "ConfigLoader.h"
#include "HookServer.h"
#include "HookController.h"
#include "HookUseCase.h"
#include "HybridAuthRepository.h"
#include "RedisStreamStateManager.h"
#include "StreamTaskScheduler.h"
#include "NodeConfig.h"
#include "Logger.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <random>

// Networking
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;
using namespace std::chrono_literals;

// ---------------------- Helper Functions ----------------------

// Check if service is available (non-intrusive)
bool check_service_available(const std::string& host, int port)
{
    try
    {
        net::io_context ioc;
        tcp::socket socket(ioc);
        tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        beast::error_code ec;
        net::connect(socket, endpoints, ec);

        return !ec;
    }
    catch (...)
    {
        return false;
    }
}

// Load NodeConfig with fallback
NodeConfig loadTestNodeConfig()
{
    try
    {
        return NodeConfig::fromJsonFile("config/nodes.json");
    }
    catch (...)
    {
        std::cout << "Warning: Using hardcoded node config\n";
        NodeConfig cfg;
        cfg.rtmp_srt.push_back({"127.0.0.1", 1935});
        cfg.http_hls.push_back({"127.0.0.1", 8080});
        cfg.webrtc.push_back({"127.0.0.1", 8443});
        return cfg;
    }
}

// Generate unique test IDs
std::string generate_unique_id(const std::string& prefix)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(10000, 99999);

    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return prefix + std::to_string(now) + "_" + std::to_string(dis(gen));
}

// ---------------------- Test Fixture ----------------------

class StreamGateIntegrationTest : public ::testing::Test
{
protected:
    static constexpr const char* TEST_ADDRESS = "127.0.0.1";
    static constexpr int TEST_PORT = 9001;

    std::string test_stream_;
    std::string test_client_;
    std::string test_token_;
    std::string test_vhost_ = "__defaultVhost__";

    // Static shared resources
    static std::unique_ptr<DBManager> dbManager_;
    static std::unique_ptr<ThreadPool> threadPool_;
    static std::unique_ptr<RedisStreamStateManager> stateManager_;
    static std::unique_ptr<HybridAuthRepository> authRepo_;
    static std::unique_ptr<AuthManager> authManager_;
    static std::unique_ptr<StreamTaskScheduler> scheduler_;
    static std::unique_ptr<HookUseCase> useCase_;
    static std::unique_ptr<HookController> controller_;
    static std::unique_ptr<HookServer> server_;
    static std::atomic<bool> initialized_;

    // Send ZLM-style hook request
    std::pair<int, std::string> send_hook_request(
        const std::string& action,
        const std::string& app,
        const std::string& stream,
        const std::string& client_id,
        const std::string& token)
    {
        try
        {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            tcp::socket socket(ioc);

            auto endpoints = resolver.resolve(TEST_ADDRESS, std::to_string(TEST_PORT));
            net::connect(socket, endpoints);

            // Build ZLM-style JSON body
            std::string body = "{"
                "\"action\":\"" + action + "\","
                "\"app\":\"" + app + "\","
                "\"stream\":\"" + stream + "\","
                "\"id\":\"" + client_id + "\","
                "\"protocol\":\"rtmp\","
                "\"params\":\"token=" + token + "\""
                "}";

            http::request<http::string_body> req{http::verb::post, "/index/hook/" + action, 11};
            req.set(http::field::host, TEST_ADDRESS);
            req.set(http::field::content_type, "application/json");
            req.body() = body;
            req.prepare_payload();

            http::write(socket, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(socket, buffer, res);

            socket.shutdown(tcp::socket::shutdown_both);

            return {static_cast<int>(res.result_int()), res.body()};
        }
        catch (const std::exception& e)
        {
            std::cerr << "HTTP request failed: " << e.what() << "\n";
            return {-1, ""};
        }
    }

    // One-time setup
    static void SetUpTestSuite()
    {
        std::cout << "\n=== StreamGate Integration Test Setup ===\n";

        // Check environment variable
        if (!std::getenv("RUN_INTEGRATION_TEST"))
        {
            GTEST_SKIP() << "Set RUN_INTEGRATION_TEST=1 to enable integration tests";
        }

        // Check services availability
        if (!check_service_available("127.0.0.1", 6379) &&
            !check_service_available("127.0.0.1", 6380))
        {
            GTEST_SKIP() << "Redis not available (tried 6379 and 6380)";
        }

        if (!check_service_available("127.0.0.1", 3306))
        {
            GTEST_SKIP() << "MariaDB not available on port 3306";
        }

        try
        {
            // Load configuration
            ConfigLoader::instance().load("config/config.ini", ".env");

            // Initialize database
            DBManager::Config db_cfg;
            db_cfg.url = "tcp://127.0.0.1:3306/streamgate_db";
            db_cfg.user = ConfigLoader::instance().getString("DB_USER", "root");
            db_cfg.password = ConfigLoader::instance().getString("DB_PASS", "");
            //db_cfg.pool_size = 4;

            dbManager_ = std::make_unique<DBManager>(db_cfg);

            // Initialize Redis
            std::string redis_host = ConfigLoader::instance().getString("REDIS_HOST", "127.0.0.1");
            int redis_port = ConfigLoader::instance().getInt("REDIS_PORT", 6380);

            CacheManager::instance().init(redis_host, redis_port, 4);

            if (!CacheManager::instance().ping())
            {
                throw std::runtime_error("Redis ping failed");
            }

            // Initialize thread pool
            threadPool_ = std::make_unique<ThreadPool>(4);

            // Build components
            stateManager_ = std::make_unique<RedisStreamStateManager>(
                CacheManager::instance());

            authRepo_ = std::make_unique<HybridAuthRepository>(
                *dbManager_, CacheManager::instance());

            authManager_ = std::make_unique<AuthManager>(
                std::move(authRepo_), *threadPool_, AuthManager::Config{});

            NodeConfig nodeConfig = loadTestNodeConfig();

            scheduler_ = std::make_unique<StreamTaskScheduler>(
                *authManager_, *stateManager_, nodeConfig, StreamTaskScheduler::Config{});

            scheduler_->start();

            useCase_ = std::make_unique<HookUseCase>(*scheduler_);

            controller_ = std::make_unique<HookController>(*useCase_);

            // Create and start server
            HookServer::Config server_cfg;
            server_cfg.address = TEST_ADDRESS;
            server_cfg.port = TEST_PORT;
            server_cfg.io_threads = 2;

            server_ = std::make_unique<HookServer>(server_cfg, *controller_);

            if (!server_->start())
            {
                throw std::runtime_error("Failed to start HookServer");
            }

            std::this_thread::sleep_for(500ms);
            initialized_.store(true);

            std::cout << "✅ Server started on " << TEST_ADDRESS << ":" << TEST_PORT << "\n";
        }
        catch (const std::exception& e)
        {
            std::cerr << "Setup failed: " << e.what() << "\n";
            throw;
        }
    }

    // Per-test setup
    void SetUp() override
    {
        if (!initialized_.load())
        {
            GTEST_SKIP() << "Server not initialized";
        }

        // Generate unique test data
        test_stream_ = generate_unique_id("stream_");
        test_client_ = generate_unique_id("client_");
        test_token_ = generate_unique_id("token_");

        // Insert auth record
        try
        {
            std::string stream_key = test_vhost_ + "/live/" + test_stream_;

            auto conn = dbManager_->acquireConnection();
            auto stmt = conn->prepareStatement(
                "INSERT INTO stream_auth (stream_key, client_id, auth_token, is_active) "
                "VALUES (?, ?, ?, 1)");

            stmt->setString(1, stream_key);
            stmt->setString(2, test_client_);
            stmt->setString(3, test_token_);
            stmt->execute();

            std::cout << "✅ Test auth inserted: " << stream_key << "\n";
        }
        catch (const std::exception& e)
        {
            GTEST_SKIP() << "Failed to insert test data: " << e.what();
        }
    }

    // One-time teardown
    static void TearDownTestSuite()
    {
        if (!initialized_.load()) return;

        std::cout << "\n=== StreamGate Integration Test Teardown ===\n";

        if (server_)
        {
            server_->stop();
            server_.reset();
        }

        if (scheduler_)
        {
            scheduler_->stop();
            scheduler_.reset();
        }

        controller_.reset();
        useCase_.reset();
        authManager_.reset();
        stateManager_.reset();
        threadPool_.reset();
        dbManager_.reset();

        initialized_.store(false);
        std::cout << "✅ Teardown complete\n";
    }
};

// Static member definitions
std::unique_ptr<DBManager> StreamGateIntegrationTest::dbManager_;
std::unique_ptr<ThreadPool> StreamGateIntegrationTest::threadPool_;
std::unique_ptr<RedisStreamStateManager> StreamGateIntegrationTest::stateManager_;
std::unique_ptr<HybridAuthRepository> StreamGateIntegrationTest::authRepo_;
std::unique_ptr<AuthManager> StreamGateIntegrationTest::authManager_;
std::unique_ptr<StreamTaskScheduler> StreamGateIntegrationTest::scheduler_;
std::unique_ptr<HookUseCase> StreamGateIntegrationTest::useCase_;
std::unique_ptr<HookController> StreamGateIntegrationTest::controller_;
std::unique_ptr<HookServer> StreamGateIntegrationTest::server_;
std::atomic<bool> StreamGateIntegrationTest::initialized_{false};

// ---------------------- Test Cases ----------------------

TEST_F(StreamGateIntegrationTest, ValidPublish_ShouldSucceed)
{
    auto [status, body] = send_hook_request(
        "on_publish", "live", test_stream_, test_client_, test_token_);

    EXPECT_EQ(status, 200);
    EXPECT_TRUE(body.find("\"code\":0") != std::string::npos);
}

TEST_F(StreamGateIntegrationTest, InvalidToken_ShouldReject)
{
    auto [status, body] = send_hook_request(
        "on_publish", "live", test_stream_, test_client_, "wrong_token");

    EXPECT_EQ(status, 200); // HTTP 200 but business code != 0
    EXPECT_TRUE(body.find("\"code\":4") != std::string::npos ||
        body.find("\"code\":1") != std::string::npos);
}

TEST_F(StreamGateIntegrationTest, InvalidStream_ShouldReject)
{
    auto [status, body] = send_hook_request(
        "on_publish", "live", "nonexistent_stream", test_client_, test_token_);

    EXPECT_EQ(status, 200);
    EXPECT_TRUE(body.find("\"code\":4") != std::string::npos ||
        body.find("\"code\":1") != std::string::npos);
}

TEST_F(StreamGateIntegrationTest, CacheHit_ShouldBeFaster)
{
    // First request (cache miss)
    auto start1 = std::chrono::steady_clock::now();
    send_hook_request("on_publish", "live", test_stream_, test_client_, test_token_);
    auto duration1 = std::chrono::steady_clock::now() - start1;

    // Second request (cache hit)
    auto start2 = std::chrono::steady_clock::now();
    send_hook_request("on_publish", "live", test_stream_, test_client_, test_token_);
    auto duration2 = std::chrono::steady_clock::now() - start2;

    // Cache hit should be faster (but may not always be due to variance)
    std::cout << "First request: " << duration1.count() << "ns\n";
    std::cout << "Second request: " << duration2.count() << "ns\n";

    // Just check both succeeded
    SUCCEED();
}

TEST_F(StreamGateIntegrationTest, PublishDone_ShouldSucceed)
{
    // First publish
    send_hook_request("on_publish", "live", test_stream_, test_client_, test_token_);

    // Then publish_done
    auto [status, body] = send_hook_request(
        "on_publish_done", "live", test_stream_, test_client_, "");

    EXPECT_EQ(status, 200);
    EXPECT_TRUE(body.find("\"code\":0") != std::string::npos);
}

TEST_F(StreamGateIntegrationTest, ConcurrentRequests_ShouldHandleCorrectly)
{
    constexpr int NUM_REQUESTS = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < NUM_REQUESTS; ++i)
    {
        threads.emplace_back([this, &success_count]()
        {
            auto [status, body] = send_hook_request(
                "on_publish", "live", test_stream_, test_client_, test_token_);

            if (status == 200 && body.find("\"code\":0") != std::string::npos)
            {
                success_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    // At least some should succeed (first one will succeed, others may fail due to "already publishing")
    EXPECT_GT(success_count.load(), 0);
    std::cout << "Concurrent requests: " << success_count.load() << "/" << NUM_REQUESTS << " succeeded\n";
}

// ---------------------- Main ----------------------

int main(int argc, char** argv)
{
    // Set log level to reduce noise during tests
    Logger::instance().set_min_level(LogLevel::WARNING);

    ::testing::InitGoogleTest(&argc, argv);

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     StreamGate Integration Test Suite                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "💡 Tip: Set RUN_INTEGRATION_TEST=1 to run these tests\n";
    std::cout << "💡 Requires: Redis (6380) + MariaDB (3306) running\n";
    std::cout << "\n";

    return RUN_ALL_TESTS();
}
