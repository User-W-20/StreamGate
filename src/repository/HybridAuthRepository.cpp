//
// Created by wxx on 12/12/25.
//
#include "HybridAuthRepository.h"
#include "Logger.h"

HybridAuthRepository::HybridAuthRepository(DBManager& dbManager, CacheManager& cacheManager)
    :_dbManager(dbManager),
_cacheManager(cacheManager),
_cacheTTL(cacheManager.getTTL())
{
    LOG_INFO("HybridAuthRepository initialized with CacheTTL: " +std::to_string(_cacheTTL)+ " seconds.");
}

std::optional<StreamAuthData> HybridAuthRepository::getAuthData(const std::string& streamKey, const std::string& clientId, const std::string& authToken)
{
    //查缓存
    auto cachedData=_cacheManager.getAuthDataFromCache(streamKey);

    if (cachedData.has_value())
    {
        LOG_INFO("AuthRepository: Cache HIT for stream " + streamKey);
        return cachedData;
    }

    LOG_INFO("AuthRepository: Cache MISS. Falling back to DB for stream " + streamKey);

    try
    {
        auto dbResult=_dbManager.getAuthDataFromDB(streamKey,clientId,authToken);

        if (dbResult.has_value())
        {
            LOG_INFO("AuthRepository: DB lookup Success.");
            _cacheManager.setAuthDataToCache(dbResult.value(),_cacheTTL);
            return dbResult;
        }else
        {
            LOG_WARN("AuthRepository: DB lookup returned no data for stream "+streamKey);
            return std::nullopt;
        }
    }catch (const std::exception&e)
    {
        LOG_ERROR("AuthRepository: DB fault during lookup for stream " +streamKey+ ": " + e.what());
        throw;
    }
}
