//
// Created by wxx on 12/11/25.
//

#ifndef STREAMGATE_HYBRIDAUTHREPOSITORY_H
#define STREAMGATE_HYBRIDAUTHREPOSITORY_H
#include "IAuthRepository.h"
#include "CacheManager.h"
#include "DBManager.h"
#include "StreamAuthData.h"

class HybridAuthRepository final : public IAuthRepository
{
public:
    HybridAuthRepository(DBManager& dbManager, CacheManager& cacheManager);

    std::optional<StreamAuthData> getAuthData(const std::string& streamKey,
                                              const std::string& clientId,
                                              const std::string& authToken) override;

private:
    DBManager& _dbManager;
    CacheManager& _cacheManager;

    const int _cacheTTL;
};

#endif //STREAMGATE_HYBRIDAUTHREPOSITORY_H