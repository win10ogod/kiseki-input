cmake_minimum_required(VERSION 3.24)

project(KisekiInput VERSION 0.1.0 LANGUAGES CXX)

option(KISEKI_BUILD_TESTING "Build Kiseki tests" ON)

include(FetchContent)
include(CTest)

FetchContent_Declare(
    CLI11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz
    URL_HASH SHA256=f2d893a65c3b1324c50d4e682c0cdc021dd0477ae2c048544f39eed6654b699a
)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
)
FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.15.3.tar.gz
    URL_HASH SHA256=2121bbf38871bb2aafb5f7f2b9b94705366170909f434428352187cb0216124e
)
FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
    URL_HASH SHA256=269543a49eb76f40b3f93ff231d4c24c27a7e16c90e47d2e45bcc564de470c6e
)

FetchContent_MakeAvailable(CLI11 nlohmann_json cpp_httplib)

set(KISEKI_CORE_SOURCES
    src/core/capabilities/capabilities_model.cpp
    src/core/config/config_store.cpp
    src/core/config/config_validation.cpp
    src/core/config/config_model.cpp
    src/core/version.cpp
)

add_library(kiseki_core STATIC ${KISEKI_CORE_SOURCES})
target_compile_features(kiseki_core PUBLIC cxx_std_20)
target_include_directories(kiseki_core PUBLIC src)
target_link_libraries(kiseki_core PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(kiseki_core PROPERTIES CXX_EXTENSIONS OFF)

add_library(kiseki_cli STATIC
    src/cli/app.cpp
)
target_compile_features(kiseki_cli PUBLIC cxx_std_20)
target_include_directories(kiseki_cli PUBLIC src)
target_link_libraries(kiseki_cli PRIVATE kiseki_core CLI11::CLI11)
set_target_properties(kiseki_cli PROPERTIES CXX_EXTENSIONS OFF)

add_executable(kiseki src/cli/main.cpp)
target_compile_features(kiseki PRIVATE cxx_std_20)
target_link_libraries(kiseki PRIVATE kiseki_cli)
set_target_properties(kiseki PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING AND KISEKI_BUILD_TESTING)
    FetchContent_MakeAvailable(Catch2)
    target_compile_features(Catch2 PUBLIC cxx_std_20)
    target_compile_features(Catch2WithMain PUBLIC cxx_std_20)
    set_target_properties(Catch2 Catch2WithMain PROPERTIES CXX_EXTENSIONS OFF)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    set(KISEKI_TEST_SOURCES
        test/capabilities/capabilities_model_test.cpp
        test/cli/cli_app_test.cpp
        test/config/config_model_test.cpp
        test/config/config_store_test.cpp
        test/config/config_validation_test.cpp
        test/smoke_test.cpp
    )

    add_executable(kiseki_tests ${KISEKI_TEST_SOURCES})
    target_compile_features(kiseki_tests PRIVATE cxx_std_20)
    target_link_libraries(kiseki_tests PRIVATE kiseki_cli kiseki_core Catch2::Catch2WithMain)
    set_target_properties(kiseki_tests PROPERTIES CXX_EXTENSIONS OFF)
    catch_discover_tests(kiseki_tests)
endif()
