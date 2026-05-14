cmake_minimum_required(VERSION 4.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/source/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/source/*.cpp")

file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/include/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/include/*.hpp")


project(data_feed CXX)

find_package(Boost REQUIRED CONFIG COMPONENTS system)
find_package(OpenSSL REQUIRED)

add_executable(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_HEADERS} ${PROJECT_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto)

if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock)
  target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0A00)
endif()
