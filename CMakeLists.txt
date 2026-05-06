cmake_minimum_required(VERSION 3.24)

project(KisekiInput VERSION 0.1.0 LANGUAGES CXX)

option(KISEKI_BUILD_TESTING "Build Kiseki tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    CLI11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz
)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.15.3.tar.gz
)
FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
)

FetchContent_MakeAvailable(CLI11 nlohmann_json cpp_httplib)

set(KISEKI_CORE_SOURCES
    src/core/version.cpp
)

add_library(kiseki_core STATIC ${KISEKI_CORE_SOURCES})
target_include_directories(kiseki_core PUBLIC src)
target_link_libraries(kiseki_core PUBLIC nlohmann_json::nlohmann_json)

add_executable(kiseki src/cli/main.cpp)
target_link_libraries(kiseki PRIVATE kiseki_core CLI11::CLI11)

if(KISEKI_BUILD_TESTING)
    FetchContent_MakeAvailable(Catch2)
    include(CTest)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    set(KISEKI_TEST_SOURCES
        test/smoke_test.cpp
    )

    add_executable(kiseki_tests ${KISEKI_TEST_SOURCES})
    target_link_libraries(kiseki_tests PRIVATE kiseki_core Catch2::Catch2WithMain)
    catch_discover_tests(kiseki_tests)
endif()
