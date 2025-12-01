//
// Created by X on 2025/11/24.
//
#include <iostream>
#include <thread>

#include "HookServer.h"
#include "ConfigLoader.h"
#include "DBManager.h"
#include "CacheManager.h"

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

        std::cout << "Starting StreamGate Server..." << std::endl;
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Server critical error: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Server stopped gracefully." << std::endl;
    return 0;
}