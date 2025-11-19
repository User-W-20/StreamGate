//
// Created by X on 2025/11/16.
//
#include "HookServer.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/StreamCopier.h"
#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Application.h"

#include "ConfigLoader.h"
#include "DBManager.h"
#include "AuthManager.h"

#include <iostream>
#include <string>

using namespace Poco::Net;
using namespace Poco::Util;

static AuthManager g_authManager;

void HookRequestHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response)
{
    response.setStatus(HTTPResponse::HTTP_OK);
    response.setContentType("application/json");

    if (request.getMethod() == HTTPServerRequest::HTTP_POST)
    {
        std::istream& is = request.stream();
        std::string body;
        Poco::StreamCopier::copyToString(is, body);

        int auth_code = g_authManager.checkHook(body);

        std::string json_response;
        if (auth_code == 0)
        {
            json_response = R"({"code":0,"msg":"Auth OK"})";
        }
        else
        {
            json_response = R"({"code":1,"msg":"Auth Failed"})";
        }

        response.send() << json_response;
    }
    else
    {
        response.setStatus(HTTPResponse::HTTP_NOT_FOUND);
        response.send() << "404 Not Found";
    }
}

HTTPRequestHandler* HookRequestHandlerFactory::createRequestHandler(const HTTPServerRequest& request)
{
    std::string url = request.getURI();
    if (url.find("/api/v1/srs") == 0)
    {
        return new HookRequestHandler();
    }
    return nullptr;
}

void StreamGateServer::initialize(Application& self)
{
    ConfigLoader::instance();

    DBManager::instance();

    ServerApplication::initialize(self);
    std::cout << "[Server] All resources (Config, DB Pool) initialized successfully." << std::endl;
}

int StreamGateServer::main(const std::vector<std::string>& args)
{
    Poco::UInt16 port = 0;
    int maxThreads = 4;

    try
    {
        port = ConfigLoader::instance().getInt("HOOK_SERVER_PORT");

        if (ConfigLoader::instance().has("SERVER_MAX_THREADS"))
        {
            maxThreads = ConfigLoader::instance().getInt("SERVER_MAX_THREADS");
        }
        else
        {
            std::cout << "[Config] Using default max threads: " << maxThreads << std::endl;
        }
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "[Server ERROR] Configuration reading failed (e.g., HOOK_SERVER_PORT missing or invalid): " << e.
            displayText() << std::endl;
        return Application::EXIT_CONFIG;
    }

    HTTPServerParams::Ptr pParams = new HTTPServerParams;
    pParams->setMaxQueued(100);
    pParams->setMaxThreads(maxThreads);

    try
    {
        ServerSocket svs(SocketAddress("0.0.0.0", port));
        HTTPServer srv(new HookRequestHandlerFactory, svs, pParams);
        srv.start();

        std::cout << "------------------------------------------------" << std::endl;
        std::cout << "StreamGate C++ Hook Server started successfully." << std::endl;
        std::cout << "Listening on port " << port << "..." << std::endl;
        std::cout << "------------------------------------------------" << std::endl;
        waitForTerminationRequest();

        srv.stop();
        return Application::EXIT_OK;
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "[Server ERROR] Failed to start HTTP Server: " << e.displayText() << std::endl;
        return Application::EXIT_SOFTWARE;
    }
}

int main(int argc, char** argv)
{
    StreamGateServer app;
    return app.run(argc, argv);
}