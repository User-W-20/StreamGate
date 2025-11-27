cmake_minimum_required(VERSION 3.10)
project(StreamGate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_PREFIX_PATH "/usr/local/boost_1_89_0" ${CMAKE_PREFIX_PATH})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(src)

#add_compile_options(-Wall -Wextra -Werror)
