//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_HOOKSERVER_CPP_H
#define STREAMGATE_HOOKSERVER_CPP_H
#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Util/ServerApplication.h"
#include "AuthManager.h"
#include "HookServer.h"

namespace Poco
{
    namespace Net
    {
        class HTTPServerRequest;
        class HTTPServerResponse;
    }

    namespace Util
    {
        class Application;
    }
}

using namespace Poco::Net;
using namespace Poco::Util;

//请求处理类
class HookRequestHandler : public HTTPRequestHandler
{
public:
    virtual void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override;
};

//请求工厂类
class HookRequestHandlerFactory : public HTTPRequestHandlerFactory
{
    virtual HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& request) override;
};

//服务器应用类
class StreamGateServer : public ServerApplication
{
protected:
    void initialize(Application& self) override;

    int main(const std::vector<std::string>& args) override;
};
#endif //STREAMGATE_HOOKSERVER_CPP_H