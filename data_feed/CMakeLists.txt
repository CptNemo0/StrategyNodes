cmake_minimum_required(VERSION 4.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Each directory is one namespace. Listed explicitly, so a recursive glob over
# the whole tree does not pick up build/.
set(PROJECT_MODULES core crypto frontend kraken l2 net)

set(PROJECT_SOURCE_GLOBS "${CMAKE_CURRENT_LIST_DIR}/main.cc")
foreach(module IN LISTS PROJECT_MODULES)
  list(APPEND PROJECT_SOURCE_GLOBS
       "${CMAKE_CURRENT_LIST_DIR}/${module}/*.cc"
       "${CMAKE_CURRENT_LIST_DIR}/${module}/*.h")
endforeach()

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_GLOBS})

project(data_feed CXX)

find_package(Boost REQUIRED CONFIG COMPONENTS system lockfree)
find_package(OpenSSL REQUIRED)

add_executable(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../third_party)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::system
    Boost::lockfree
    OpenSSL::SSL
    OpenSSL::Crypto)

if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock crypt32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_options(${PROJECT_NAME} PRIVATE "-Wa,-mbig-obj")
endif()

# GCC keeps the C++23 <print> terminal helpers (std::__open_terminal,
# std::__write_to_terminal) in a separate library that is not linked by default.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_libraries(${PROJECT_NAME} PRIVATE stdc++exp)
endif()
