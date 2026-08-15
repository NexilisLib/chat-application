cmake_minimum_required(VERSION 3.10)
project(chat-client)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../nexilis/nexilis/build/")

# Add nexilis
find_package(Nexilis REQUIRED CONFIG)

# Find other required packages
find_package(PkgConfig REQUIRED)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

# Create the client executable
add_executable(chat-client
    main.cc
)

# Link libraries
target_link_libraries(chat-client 
    PRIVATE
    ${CURSES_LIBRARIES}
    Threads::Threads
    Nexilis::nexilis
)

# Include directories
target_include_directories(chat-client PRIVATE
    ${CURSES_INCLUDE_DIR}
    nexilis/nexilis/include
    src
)

# Compiler options
target_compile_options(chat-client PRIVATE -Wall -Wextra)
