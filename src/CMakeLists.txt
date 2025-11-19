find_package(Poco REQUIRED COMPONENTS Net Util JSON Foundation Data DataMySQL)
find_package(PkgConfig REQUIRED)

pkg_check_modules(MARIADB REQUIRED libmariadb)


set(SOURCES
        main/HookServer.cpp
        auth/AuthManager.cpp
        db/DBManager.cpp
        util/ConfigLoader.cpp
)

add_executable(streamgate_hook_server ${SOURCES})

target_include_directories(streamgate_hook_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${MARIADB_INCLUDE_DIRS}
)

target_link_directories(streamgate_hook_server PRIVATE
        ${MARIADB_LIBRARY_DIRS}
)


target_link_libraries(streamgate_hook_server PRIVATE
        PocoFoundation
        PocoNet
        PocoUtil
        PocoJSON

        PocoData
        PocoDataMySQL

        ${MARIADB_LIBRARIES}
)