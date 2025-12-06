//
// Created by X on 2025/11/24.
//
#include <iostream>
#include <thread>

#include "HookServer.h"
#include "ConfigLoader.h"
#include "DBManager.h"
#include "CacheManager.h"
#include "Logger.h"

int main(int argc, char* argv[])
{
    const std::string ini_path = "config/config.ini";
    const std::string env_path = ".env";

    try
    {
        ConfigLoader::instance().load(ini_path, env_path);

        CacheManager::instance().start_io_loop();
        DBManager::instance().connect();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        ConfigLoader& config = ConfigLoader::instance();
        const std::string address = config.getString("SERVER_ADDRESS");
        const int port = config.getInt("SERVER_PORT");
        const int io_threads = config.getInt("SERVER_MAX_THREADS");

        StreamGateServer server(address, port, io_threads);

        LOG_INFO("Starting StreamGate Server...");

        server.run();
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("Server critical error: "+std::string(e.what()));
        std::cerr << "Fatal error during startup: " << e.what() << std::endl;
        return 1;
    }

    LOG_INFO("Server stopped gracefully.");
    return 0;
}