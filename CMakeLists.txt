cmake_minimum_required(VERSION 3.14)
project(sdn_controller_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

add_library(sdn STATIC
  src/topology.cpp src/network.cpp src/sdn_switch.cpp src/controller.cpp)
target_include_directories(sdn PUBLIC include)
target_link_libraries(sdn PUBLIC Threads::Threads)

add_executable(sdn_sim src/main.cpp)
target_link_libraries(sdn_sim sdn)

enable_testing()
add_executable(tests tests/test_all.cpp)
target_link_libraries(tests sdn)
add_test(NAME all COMMAND tests)
