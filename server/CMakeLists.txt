cmake_minimum_required(VERSION 3.30)
project(chat-application-server)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../nexilis/nexilis/build")
find_package(Nexilis REQUIRED CONFIG)

add_executable(chat-application-server server.cc)
target_include_directories(chat-application-server PRIVATE "${CMAKE_SOURCE_DIR}/..")
target_link_libraries(chat-application-server PRIVATE Nexilis::nexilis)
