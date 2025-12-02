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
    try
    {
        ConfigLoader::instance().load("config/config.ini");

        ConfigLoader::instance().load(".env");

        CacheManager::instance().start_io_loop();

        DBManager::instance().connect();

        std::this_thread::sleep_for((std::chrono::milliseconds(500)));

        StreamGateServer server;

        LOG_INFO("Starting StreamGate Server...");
        server.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Server critical error: " +std::string(e.what()));
        return 1;
    }
    LOG_INFO("Server stopped gracefully.");
    return 0;
}