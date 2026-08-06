cmake_minimum_required(VERSION 3.16)
project(stock_monitor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/curl/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/cJSON/CMakeLists.txt")
    message(FATAL_ERROR "third_party submodules are missing. Run: git submodule update --init --recursive")
endif()

# ---- cJSON (vendored, static) ----
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cJSON EXCLUDE_FROM_ALL)

# ---- curl (vendored, static, trimmed to what this tool needs) ----
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/curl EXCLUDE_FROM_ALL)

# ---- stock_monitor ----
add_executable(stock_monitor monitor.c)
target_link_libraries(stock_monitor PRIVATE libcurl_static cjson)

file(COPY_FILE "${CMAKE_SOURCE_DIR}/tickers.txt" "${CMAKE_BINARY_DIR}/tickers.txt" ONLY_IF_DIFFERENT)
