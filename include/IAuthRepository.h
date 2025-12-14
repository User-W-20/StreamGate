//
// Created by wxx on 12/11/25.
//

#ifndef STREAMGATE_IAUTHREPOSITORY_H
#define STREAMGATE_IAUTHREPOSITORY_H
#include "StreamAuthData.h"
#include <optional>
#include <string>

class IAuthRepository
{
public:
    virtual ~IAuthRepository() = default;

    virtual std::optional<StreamAuthData> getAuthData(const std::string& streamKey,
                                                      const std::string& clientId,
                                                      const std::string& authToken) =0;
};
#endif //STREAMGATE_IAUTHREPOSITORY_H