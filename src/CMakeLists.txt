set(SOURCES
        main/HookServer.cpp
        auth/AuthManager.cpp
)

add_executable(streamgate_hook_server ${SOURCES})

target_include_directories(streamgate_hook_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(streamgate_hook_server PRIVATE
        PocoNet
        PocoUtil
        PocoJSON
        PocoFoundation
)