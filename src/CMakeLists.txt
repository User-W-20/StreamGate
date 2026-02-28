# ============================================================
# 第三方依赖查找
# ============================================================
# Boost（仅使用 json 组件）
find_package(Boost 1.89 REQUIRED COMPONENTS json)

# pkg-config（用于查找 hiredis）
find_package(PkgConfig REQUIRED)

# Google Test（单元测试）
find_package(GTest REQUIRED)

# nlohmann/json（现代 C++ JSON 库）
find_package(nlohmann_json REQUIRED)

# 1. 查找 hiredis (redis++ 的底层依赖)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# 2. 查找 redis++ 库文件 (针对 Fedora /usr/local/lib64 路径优化)
find_library(REDISPP_LIB redis++
        HINTS /usr/local/lib64 /usr/lib64
)

if (NOT REDISPP_LIB)
    message(FATAL_ERROR "libredis++.so not found! Please check your installation.")
endif()

message(STATUS "Found Redis++: ${REDISPP_LIB}")

# ============================================================
# MariaDB Connector/C++ 相关库
# ============================================================

# MariaDB C++ Connector（libmariadbcpp.so）
find_library(MARIADB_CPP mariadbcpp
        HINTS /usr/lib64/mariadb /usr/local/lib64/mariadb
)

# MariaDB C Client（libmariadb.so，C++ Connector 依赖）
find_library(MARIADB_CLIENT mariadb
        HINTS /usr/lib64/mariadb /usr/local/lib64/mariadb
)

# 如果没找到库，直接中断配置
if (NOT MARIADB_CPP)
    message(FATAL_ERROR "libmariadbcpp not found")
endif ()

if (NOT MARIADB_CLIENT)
    message(FATAL_ERROR "libmariadb not found")
endif ()

message(STATUS "Found MariaDB C++: ${MARIADB_CPP}")
message(STATUS "Found MariaDB C client: ${MARIADB_CLIENT}")


# ============================================================
# 核心库：streamgate_core
# ============================================================

add_library(streamgate_core
        auth/AuthManager.cpp
        cache/CacheManager.cpp
        db/DBManager.cpp
        util/ConfigLoader.cpp
        util/Logger.cpp
        models/StreamAuthData.cpp
        repository/HybridAuthRepository.cpp
        util/ThreadPool.cpp
        main/HookServer.cpp
        util/StreamTaskSerializer.cpp
        repository/RedisStreamStateManager.cpp
        scheduler/StreamTaskScheduler.cpp
        util/EnumToString.cpp
        util/NodeConfig.cpp
        util/ZlmHookCommon.cpp
        main/HookController.cpp
        util/HookUseCase.cpp
        metrics/MetricsCollector.cpp
        metrics/ServerMetricsProvider.cpp
        metrics/SchedulerMetricsProvider.cpp
        metrics/CacheMetricsProvider.cpp
        metrics/DatabaseMetricsProvider.cpp
        util/HealthChecker.cpp
)

# core 库的头文件搜索路径
# PUBLIC：依赖 core 的 target 也能自动继承这些 include 路径
target_include_directories(streamgate_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CPP_REDIS_INCLUDE_DIRS}
        /usr/local/include
        /usr/include/mariadb
)

# 编译选项说明：
# -Wall -Wextra ：开启常用警告（观察型）
# -Werror       ：仅对 C++ 代码生效，把 warning 当成 error
#                 只作用于你自己的 core 代码，不影响第三方库
target_compile_options(streamgate_core PRIVATE
        -Wall
        -Wextra
        $<$<COMPILE_LANGUAGE:CXX>:-Werror>
)

# core 库需要链接的依赖
# PUBLIC：依赖 core 的可执行文件会自动继承这些库
target_link_libraries(streamgate_core PUBLIC
        pthread
        Boost::json

        # MariaDB C++ Connector 及其 C Client
        ${MARIADB_CPP}
        ${MARIADB_CLIENT}

        ${REDISPP_LIB}
        ${HIREDIS_LIBRARIES}

        # nlohmann/json
        nlohmann_json::nlohmann_json
)

# ============================================================
# 主程序：streamgate_hook_server
# ------------------------------------------------------------
# 只包含 main 相关代码，所有业务逻辑来自 core
# ============================================================

add_executable(streamgate_hook_server
        main/main.cpp

)

# 主程序只需要链接 core
target_link_libraries(streamgate_hook_server PRIVATE
        streamgate_core
)

# ============================================================
# 单元测试程序：test01
# ------------------------------------------------------------
# 使用 Google Test，复用 core 里的业务逻辑
# ============================================================


add_executable(test01
        test/test_auth.cpp

)

target_link_libraries(test01 PRIVATE
        streamgate_core
        GTest::GTest
        GTest::Main
)

# ============================================================
# RPATH 设置（用于运行时找到动态库）
# ============================================================

# 自动把依赖库路径写入 RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# 构建阶段就使用 RPATH（方便直接运行）
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

