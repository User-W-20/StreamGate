//
// Created by X on 2025/11/16.
//
#include "HookServer.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/StreamCopier.h"
#include <iostream>
#include <string>

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
    ServerApplication::initialize(self);
}

int StreamGateServer::main(const std::vector<std::string>& args)
{
    Poco::UInt16 port = 8001;

    HTTPServerParams::Ptr pParams = new HTTPServerParams;
    pParams->setMaxQueued(100);
    pParams->setMaxThreads(4);

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

int main(int argc,char** argv)
{
    StreamGateServer app;
    return app.run(argc,argv);
}