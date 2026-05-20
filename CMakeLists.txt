cmake_minimum_required(VERSION 3.16)
project(neural LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt REQUIRED)

find_package(SQLite3 REQUIRED)

include(FetchContent)

FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG master
)

FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.x
)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG develop
)

FetchContent_MakeAvailable(asio spdlog nlohmann_json)

include_directories(
    include
)

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

target_include_directories(neural PRIVATE ${asio_SOURCE_DIR}/include)

target_link_libraries(neural PRIVATE
    SQLite::SQLite3
    spdlog::spdlog
    fmt::fmt
    nlohmann_json::nlohmann_json
    pthread
)

if(MSVC)
    target_compile_options(neural PRIVATE /W4)
else()
    target_compile_options(neural PRIVATE -Wall -Wextra -pedantic)
endif()

target_link_options(neural PRIVATE -Wl,-Bsymbolic -pthread)
