cmake_minimum_required(VERSION 4.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Each directory is one namespace. Listed explicitly, so a recursive glob over
# the whole tree does not pick up build/.
set(PROJECT_MODULES coinbase core crypto frontend kraken l2 net venue)

set(PROJECT_SOURCE_GLOBS)
foreach(module IN LISTS PROJECT_MODULES)
  list(APPEND PROJECT_SOURCE_GLOBS
       "${CMAKE_CURRENT_LIST_DIR}/${module}/*.cc"
       "${CMAKE_CURRENT_LIST_DIR}/${module}/*.h")
endforeach()

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_GLOBS})

# Tests sit next to the code they cover as `[name]_unittest.cc`, but build into
# their own executable rather than into data_feed.
set(TEST_SOURCES ${PROJECT_SOURCES})
list(FILTER TEST_SOURCES INCLUDE REGEX "_unittest\\.cc$")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "_unittest\\.cc$")

project(data_feed CXX)

find_package(Boost REQUIRED CONFIG COMPONENTS system lockfree)
find_package(OpenSSL REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# The wire contract with the frontend, generated from proto/. A target of its
# own keeps the generated code out of PROJECT_SOURCES, and so out of
# clang-tidy; SYSTEM keeps its warnings out of ours. Included as
# "market_data.pb.h".
set(PROTO_DIR "${CMAKE_CURRENT_LIST_DIR}/../proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

# Run by hand rather than through protobuf_generate, which cannot set protoc's
# environment: protoc links the compiler's runtime DLLs, so the compiler's
# directory must come first on PATH, ahead of any incompatible
# libstdc++-6.dll (Git for Windows ships one).
get_filename_component(COMPILER_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
add_custom_command(
    OUTPUT ${PROTO_OUT_DIR}/market_data.pb.h ${PROTO_OUT_DIR}/market_data.pb.cc
    COMMAND ${CMAKE_COMMAND} -E env
            --modify PATH=path_list_prepend:${COMPILER_BIN_DIR} --
            $<TARGET_FILE:protobuf::protoc> --cpp_out=${PROTO_OUT_DIR}
            -I ${PROTO_DIR} ${PROTO_DIR}/market_data.proto
    DEPENDS ${PROTO_DIR}/market_data.proto protobuf::protoc
    COMMENT "Generating C++ from market_data.proto"
    VERBATIM)

add_library(market_data_proto STATIC ${PROTO_OUT_DIR}/market_data.pb.cc)
target_include_directories(market_data_proto SYSTEM PUBLIC ${PROTO_OUT_DIR})
target_link_libraries(market_data_proto PUBLIC protobuf::libprotobuf)

# Every module, compiled once and linked into both data_feed and
# data_feed_unittests. An object library rather than a static one, so objects
# nothing references by name still link.
add_library(${PROJECT_NAME}_modules OBJECT ${PROJECT_SOURCES})
target_include_directories(${PROJECT_NAME}_modules PUBLIC
    ${CMAKE_CURRENT_LIST_DIR})

target_include_directories(${PROJECT_NAME}_modules SYSTEM PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../third_party)

target_link_libraries(${PROJECT_NAME}_modules PUBLIC
    market_data_proto
    Boost::system
    Boost::lockfree
    OpenSSL::SSL
    OpenSSL::Crypto)

if(WIN32)
  target_link_libraries(${PROJECT_NAME}_modules PUBLIC ws2_32 mswsock crypt32)
  target_compile_definitions(${PROJECT_NAME}_modules PUBLIC
      _WIN32_WINNT=0x0A00)
  target_compile_options(${PROJECT_NAME}_modules PUBLIC "-Wa,-mbig-obj")
endif()

# GCC keeps the C++23 <print> terminal helpers (std::__open_terminal,
# std::__write_to_terminal) in a separate library that is not linked by default.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_libraries(${PROJECT_NAME}_modules PUBLIC stdc++exp)
endif()

# A libstdc++ configured without atomic builtins (as WinLibs GCC 16.1.0 was)
# makes its reference count helper (__gnu_cxx::__exchange_and_add, behind every
# shared_ptr copy and release) a plain add under a mutex, while
# weak_ptr::lock() and shared_from_this() update the same count with a
# lock-free compare-and-swap that ignores that mutex. When the two race, an
# update is lost and a shared_ptr frees an object still in use. The prebuilt
# libstdc++ DLL and every vcpkg package carry that helper too, so no define of
# ours can repair it: refuse such a toolchain. See
# docs/bad_weak_ptr_postmortem.md; core/shared_ptr_unittest.cc checks it at
# run time.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([[
  #include <version>
  #if defined(__GLIBCXX__) && !defined(_GLIBCXX_ATOMIC_WORD_BUILTINS)
  #error libstdc++ built without atomic builtins
  #endif
  int main() {}
]] LIBSTDCXX_HAS_ATOMIC_BUILTINS)
if(NOT LIBSTDCXX_HAS_ATOMIC_BUILTINS)
  message(FATAL_ERROR
      "${CMAKE_CXX_COMPILER}'s libstdc++ was built without atomic builtins "
      "(_GLIBCXX_ATOMIC_WORD_BUILTINS is undefined in bits/c++config.h), so "
      "std::shared_ptr reference counts race. Use a toolchain built with "
      "them, such as MSYS2 UCRT64 GCC.")
endif()

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/main.cc)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_modules)

add_executable(${PROJECT_NAME}_unittests ${TEST_SOURCES})
target_link_libraries(${PROJECT_NAME}_unittests PRIVATE
    ${PROJECT_NAME}_modules
    GTest::gtest
    GTest::gmock
    GTest::gtest_main)

# Ship the compiler's runtime DLLs next to each executable. Windows looks there
# before PATH, so a run from any shell loads these, not another toolchain's
# libstdc++-6.dll (an older WinLibs one, or Git for Windows').
if(MINGW)
  foreach(target IN ITEMS ${PROJECT_NAME} ${PROJECT_NAME}_unittests)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${COMPILER_BIN_DIR}/libstdc++-6.dll
                ${COMPILER_BIN_DIR}/libgcc_s_seh-1.dll
                ${COMPILER_BIN_DIR}/libwinpthread-1.dll
                $<TARGET_FILE_DIR:${target}>
        VERBATIM)
  endforeach()
endif()

enable_testing()
add_test(NAME ${PROJECT_NAME}_unittests COMMAND ${PROJECT_NAME}_unittests)
