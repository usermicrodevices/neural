cmake_minimum_required(VERSION 3.16)
project(neural LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(fmt REQUIRED)
find_package(SQLite3 REQUIRED)
find_path(ASIO_INCLUDE_DIR asio.hpp PATHS /usr/include /usr/local/include ${CMAKE_SOURCE_DIR}/third_party/asio/include PATH_SUFFIXES asio)
if(NOT ASIO_INCLUDE_DIR)
  message(FATAL_ERROR "ASIO not found. Place asio.hpp in /usr/include, /usr/local/include, or ${CMAKE_SOURCE_DIR}/third_party/asio/include")
endif()
add_library(asio::asio INTERFACE IMPORTED)
target_include_directories(asio::asio INTERFACE ${ASIO_INCLUDE_DIR})
find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h PATHS /usr/include /usr/local/include ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)
if(NOT SPDLOG_INCLUDE_DIR)
  message(FATAL_ERROR "spdlog not found. Place spdlog.h in /usr/include, /usr/local/include, or ${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
endif()
add_library(spdlog::spdlog INTERFACE IMPORTED)
target_include_directories(spdlog::spdlog INTERFACE ${SPDLOG_INCLUDE_DIR})
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp PATHS /usr/include /usr/local/include ${CMAKE_SOURCE_DIR}/third_party/json/include)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
  message(FATAL_ERROR "nlohmann_json not found. Place json.hpp in /usr/include, /usr/local/include, or ${CMAKE_SOURCE_DIR}/third_party/json/include")
endif()
add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
target_include_directories(nlohmann_json::nlohmann_json INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
include_directories(include)
add_executable(neural
    src/admin.cpp
    src/client.cpp
    src/ipc.cpp
    src/logger.cpp
    src/neural.cpp
    src/store.cpp
    src/vocabulary.cpp
    src/worker_admin.cpp
    src/worker_client.cpp
    src/worker_master.cpp
    src/main.cpp
)
target_link_libraries(neural PRIVATE
    SQLite::SQLite3
    spdlog::spdlog
    fmt::fmt
    nlohmann_json::nlohmann_json
    asio::asio
    pthread
)
if(MSVC)
    target_compile_options(neural PRIVATE /W4)
else()
    target_compile_options(neural PRIVATE -Wall -Wextra -pedantic)
endif()
target_link_options(neural PRIVATE -Wl,-Bsymbolic -pthread)