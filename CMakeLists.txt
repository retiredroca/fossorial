cmake_minimum_required(VERSION 3.14)
project(fossorial VERSION 0.1.0 LANGUAGES CXX)

add_library(fossorial INTERFACE)
target_include_directories(fossorial INTERFACE include)
target_compile_features(fossorial INTERFACE cxx_std_17)

add_executable(test_gophercore test/test_gophercore.cpp)
target_link_libraries(test_gophercore fossorial)

add_executable(test_gopherfilesystem test/test_gopherfilesystem.cpp)
target_link_libraries(test_gopherfilesystem fossorial)
target_compile_definitions(test_gopherfilesystem PRIVATE FOSSORIAL_GOPHER_ROOT="/tmp/fossorial_test")

add_executable(fossorial-server src/fossorial_server.cpp)
target_link_libraries(fossorial-server fossorial)
target_compile_definitions(fossorial-server PRIVATE FOSSORIAL_GOPHER_ROOT=".")
