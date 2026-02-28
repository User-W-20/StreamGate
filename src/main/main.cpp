//
// Created by X on 2025/11/24.
//
#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>

#include "Logger.h"
#include "ConfigLoader.h"
#include "DBManager.h"
#include "CacheManager.h"
#include "AuthManager.h"
#include "HookUseCase.h"
#include "HookController.h"
#include "HookServer.h"
#include "HybridAuthRepository.h"
#include "RedisStreamStateManager.h"
#include "MetricsCollector.h"
#include "ServerMetricsProvider.h"
#include "SchedulerMetricsProvider.h"
#include "CacheMetricsProvider.h"
#include "DatabaseMetricsProvider.h"
#include "MetricsRegistry.h"
#include "HealthChecker.h"

// 强制链接所有Provider
extern "C" void ForceLink_ServerMetricsProvider();
extern "C" void ForceLink_SchedulerMetricsProvider();
extern "C" void ForceLink_CacheMetricsProvider();
extern "C" void ForceLink_DatabaseMetricsProvider();

// 全局退出信号上下文
struct ShutdownContext
{
    std::atomic<bool> running{true};
    std::condition_variable cv;
    std::mutex mtx;
} g_ctx;

//信号处理
void signalHandler(int sig)
{
    LOG_INFO("Signal (" + std::to_string(sig) + ") received, initiating shutdown...");
    g_ctx.running = false;
    g_ctx.cv.notify_all();
}

int main(int argc, char* argv[])
{
    ForceLink_ServerMetricsProvider();
    ForceLink_SchedulerMetricsProvider();
    ForceLink_CacheMetricsProvider();
    ForceLink_DatabaseMetricsProvider();

    //加载配置
    const std::string ini_path = "config/config.ini";
    const std::string env_path = ".env";
    const std::string nodes_json_path = "config/nodes.json";

    try
    {
        std::unique_ptr<HookServer> server;
        std::unique_ptr<HookController> controller;
        std::unique_ptr<HookUseCase> use_case;
        std::unique_ptr<StreamTaskScheduler> scheduler;
        std::unique_ptr<AuthManager> auth_manager;
        std::unique_ptr<RedisStreamStateManager> state_manager;
        std::unique_ptr<DBManager> db_manager;
        // ================================================================
        // Configuration & Logger
        // ================================================================
        LOG_INFO("=== StreamGate Service Starting ===");

        ConfigLoader::LoadOptions load_opts;
        load_opts.allow_missing_ini = false;
        load_opts.allow_missing_env = true;
        load_opts.override_from_environment = true;

        ConfigLoader::instance().load(ini_path, env_path, load_opts);
        LOG_INFO("Configuration loaded successfully");

        // Configure Logger
        Logger::Config log_cfg;
        log_cfg.min_level = static_cast<LogLevel>(
            ConfigLoader::instance().getInt("LOG_LEVEL", 1));

        log_cfg.log_to_console = ConfigLoader::instance().getBool("LOG_TO_CONSOLE", true);
        log_cfg.log_to_file = ConfigLoader::instance().getBool("LOG_TO_FILE", false);
        log_cfg.log_file_path = ConfigLoader::instance().getString("LOG_FILE_PATH", "streamgate.log");
        Logger::instance().set_config(log_cfg);

        // Register signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // ================================================================
        //Infrastructure (Database & Cache)
        // ================================================================
        std::string db_host = ConfigLoader::instance().getString("DB_HOST", "127.0.0.1");
        std::string db_port = ConfigLoader::instance().getString("DB_PORT", "3306");
        std::string db_name = ConfigLoader::instance().getString("DB_NAME", "streamgate_db");
        std::string db_user = ConfigLoader::instance().getString("DB_USER", "root");
        std::string db_pass = ConfigLoader::instance().getString("DB_PASS", "");

        LOG_INFO("Connecting to database: " + db_user + "@" + db_host + ":" + db_port + "/" + db_name);

        DBManager::Config db_cfg;
        db_cfg.url = "tcp://" + db_host + ":" + db_port + "/" + db_name;
        db_cfg.user = db_user;
        db_cfg.password = db_pass;
        db_cfg.minSize = ConfigLoader::instance().getInt("DB_MIN_SIZE", 2);
        db_cfg.maxSize = ConfigLoader::instance().getInt("DB_MAX_SIZE", 10);
        db_cfg.checkoutTimeoutMs = ConfigLoader::instance().getInt("DB_TIMEOUT_MS", 5000);

        db_manager = std::make_unique<DBManager>(db_cfg);
        LOG_INFO("Database connection pool initialized");

        // Cache (Redis)
        std::string redis_host = ConfigLoader::instance().getString("REDIS_HOST", "127.0.0.1");
        int redis_port = ConfigLoader::instance().getInt("REDIS_PORT", 6380);
        // int redis_db = ConfigLoader::instance().getInt("REDIS_DB", 0);
        std::string redis_pass = ConfigLoader::instance().getString("REDIS_PASS", "");
        int cache_pool_size = ConfigLoader::instance().getInt("DB_POOL_SIZE", 8);

        CacheManager::instance().init(redis_host, redis_port, cache_pool_size, redis_pass);

        if (!CacheManager::instance().ping())
        {
            LOG_FATAL("Redis connection failed. Check REDIS_HOST and REDIS_PORT.");
            return EXIT_FAILURE;
        }
        LOG_INFO("Redis connection verified");

        // ================================================================
        // Business Components
        // ================================================================
        // Thread pool for async operations
        int pool_size = ConfigLoader::instance().getInt("THREAD_POOL_SIZE", 4);
        ThreadPool task_pool(pool_size);
        LOG_INFO("ThreadPool initialized with " + std::to_string(pool_size) + " workers");

        // Stream state management
        state_manager = std::make_unique<RedisStreamStateManager>(
            CacheManager::instance());

        // Authentication
        auto auth_repo = std::make_unique<HybridAuthRepository>(
            *db_manager,
            CacheManager::instance());

        AuthManager::Config auth_cfg;
        auth_cfg.timeout = std::chrono::milliseconds(
            ConfigLoader::instance().getInt("AUTH_TIMEOUT_MS", 5000));
        auth_manager = std::make_unique<AuthManager>(
            std::move(auth_repo),
            task_pool,
            auth_cfg);
        LOG_INFO("AuthManager initialized");

        // Node configuration
        NodeConfig::ValidationOptions node_opts;
        node_opts.allow_empty_endpoints = false;
        node_opts.strict_port_rang = true;
        node_opts.require_valid_hosts = true;

        NodeConfig node_cfg;
        try
        {
            node_cfg = NodeConfig::fromJsonFile(nodes_json_path, node_opts);

            size_t total = node_cfg.rtmp_srt.size() + node_cfg.http_hls.size() + node_cfg.webrtc.size();
            LOG_INFO("Node configuration loaded: " + std::to_string(total) + " endpoints");
        }
        catch (const std::exception& e)
        {
            LOG_WARN("Failed to load nodes.json, using defaults: " + std::string(e.what()));
        }

        // Task scheduler
        StreamTaskScheduler::Config scheduler_cfg;
        scheduler_cfg.task_timeout = std::chrono::seconds(
            ConfigLoader::instance().getInt("SCHEDULER_TIMEOUT_SEC", 60)
        );

        scheduler = std::make_unique<StreamTaskScheduler>(
            *auth_manager,
            *state_manager,
            std::move(node_cfg),
            scheduler_cfg);

        scheduler->start();
        LOG_INFO("StreamTaskScheduler started");

        // ================================================================
        // Monitoring System
        // ================================================================
        LOG_INFO("=== Initializing Monitoring System ===");

        //从ELF段自动发现所有Provider
        auto providers = MetricsRegistry::createAll();
        LOG_INFO("Discovered " + std::to_string(providers.size()) + " monitoring providers");

        // 设置依赖（延迟注入）
        for (auto& provider : providers)
        {
            // SchedulerMetricsProvider需要scheduler
            if (auto* sp = dynamic_cast<SchedulerMetricsProvider*>(provider.get()))
            {
                sp->setScheduler(scheduler.get());
                LOG_INFO("  -> Injected scheduler into SchedulerMetricsProvider");
            }
            // CacheMetricsProvider需要cache
            else if (auto* cp = dynamic_cast<CacheMetricsProvider*>(provider.get()))
            {
                cp->setCache(&CacheManager::instance());
                LOG_INFO("  -> Injected cache into CacheMetricsProvider");
            }
            // DatabaseMetricsProvider需要db
            else if (auto* dp = dynamic_cast<DatabaseMetricsProvider*>(provider.get()))
            {
                dp->setDB(db_manager.get());
                LOG_INFO("  -> Injected db into DatabaseMetricsProvider");
            }
            // ServerMetricsProvider不需要依赖（使用Thread-Local）
        }

        //注册到全局MetricsCollector
        auto& metricsCollector = MetricsCollector::instance();
        for (auto& provider : providers)
        {
            metricsCollector.registerProvider(provider);
        }
        LOG_INFO("All providers registered to MetricsCollector");

        //启动刷新线程
        metricsCollector.start(
            std::chrono::seconds(1),
            [](const nlohmann::json& report)
            {
            }
        );

        // 创建健康检查器
        auto healthChecker = std::make_shared<HealthChecker>(
            &CacheManager::instance(),
            db_manager.get(),
            scheduler.get()
        );

        LOG_INFO("=== Monitoring System Ready ===");

        // ================================================================
        //  Hook Processing Layers (Clean Architecture)
        // ================================================================

        // Business logic layer
        use_case = std::make_unique<HookUseCase>(*scheduler);

        // Routing layer
        controller = std::make_unique<HookController>(*use_case);

        // Infrastructure layer (HTTP server)
        HookServer::Config server_cfg;
        server_cfg.address = ConfigLoader::instance().getString("SERVER_ADDRESS", "0.0.0.0");
        server_cfg.port = ConfigLoader::instance().getInt("SERVER_PORT", 8080);
        server_cfg.io_threads = ConfigLoader::instance().getInt("SERVER_IO_THREADS", 2);

        server = std::make_unique<HookServer>(server_cfg, *controller);
        server->start();

        LOG_INFO("HookServer listening on " + server_cfg.address + ":" + std::to_string(server_cfg.port));
        LOG_INFO("=== StreamGate Service is Ready ===");

        // ================================================================
        // Main Event Loop
        // ================================================================
        {
            std::unique_lock<std::mutex> lock(g_ctx.mtx);
            g_ctx.cv.wait(lock, []
            {
                return !g_ctx.running.load();
            });
        }

        // ================================================================
        // Graceful Shutdown
        // ================================================================
        LOG_INFO("=== Initiating Graceful Shutdown ===");

        LOG_INFO("Stopping monitoring system...");
        MetricsCollector::instance().stop();
        LOG_INFO("Monitoring system stopped");

        // Stop in reverse order of initialization
        server->stop();
        server.reset();

        controller.reset();
        use_case.reset();

        scheduler->stop();
        scheduler.reset();

        auth_manager.reset();
        state_manager.reset();

        task_pool.stop_and_wait();
        task_pool.reset_stats();

        db_manager->shutdown();
        db_manager.reset();

        LOG_INFO("=== StreamGate Service Exited Cleanly ===");
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("Uncaught exception: " + std::string(e.what()));
        std::cerr << "FATAL: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
