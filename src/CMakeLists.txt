find_package(Boost 1.83.0 REQUIRED COMPONENTS system json)
find_package(PkgConfig REQUIRED)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})


# cpp_redis
pkg_check_modules(CPP_REDIS REQUIRED cpp_redis)

# ============================
# MariaDB Connector/C++
# ============================
find_library(MARIADB_CPP mariadbcpp
        HINTS /usr/lib /usr/lib/x86_64-linux-gnu
)

find_library(MARIADB_CLIENT mariadb
        HINTS /usr/lib /usr/lib/x86_64-linux-gnu
)

if (NOT MARIADB_CPP)
    message(FATAL_ERROR "libmariadbcpp not found")
endif()

if (NOT MARIADB_CLIENT)
    message(FATAL_ERROR "libmariadb not found")
endif()

message(STATUS "Found MariaDB C++: ${MARIADB_CPP}")
message(STATUS "Found MariaDB C client: ${MARIADB_CLIENT}")

set(SOURCES
        main/HookServer.cpp
        main/main.cpp

        auth/AuthManager.cpp
        cache/CacheManager.cpp
        db/DBManager.cpp
        util/ConfigLoader.cpp

)

set(TEST_SOURCES
        test/test_auth.cpp

        auth/AuthManager.cpp
        cache/CacheManager.cpp
        db/DBManager.cpp
        util/ConfigLoader.cpp)

add_executable(streamgate_hook_server ${SOURCES})

add_executable(test01 ${TEST_SOURCES})

target_include_directories(streamgate_hook_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CPP_REDIS_INCLUDE_DIRS}
        /usr/include/mariadb
)

target_link_libraries(streamgate_hook_server PRIVATE
        pthread
        Boost::system
        Boost::json

        # MariaDB C++ Connector
        ${MARIADB_CPP}
        ${MARIADB_CLIENT}

        # redis
        ${CPP_REDIS_LIBRARIES}
        tacopie
)

target_link_libraries(test01 PRIVATE
        pthread
        Boost::system
        Boost::json

        # MariaDB C++ Connector
        ${MARIADB_CPP}
        ${MARIADB_CLIENT}

        ${GTEST_LIBRARIES}

        # redis
        ${CPP_REDIS_LIBRARIES}
        tacopie
)
