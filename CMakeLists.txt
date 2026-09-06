cmake_minimum_required(VERSION 3.24)

project(KisekiInput VERSION 0.1.0 LANGUAGES CXX)

option(KISEKI_BUILD_TESTING "Build Kiseki tests" ON)
option(KISEKI_BUILD_IBINPUTSIMULATOR "Build and copy IbInputSimulator.dll on Windows" ON)

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

if(UNIX AND NOT APPLE AND NOT WIN32)
    find_package(X11)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(KISEKI_GIO QUIET IMPORTED_TARGET gio-2.0)
        pkg_check_modules(KISEKI_GDK_PIXBUF QUIET IMPORTED_TARGET gdk-pixbuf-2.0)
    endif()
endif()

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

add_library(kiseki_platform STATIC
    src/platform/capture/bitmap_writer.cpp
    src/platform/capture/screenshot.cpp
    src/platform/input/input.cpp
    src/platform/input/sequence_support.cpp
    src/platform/notification/notification.cpp
    src/platform/observe/ui_observation.cpp
    src/platform/permissions/permissions.cpp
    src/platform/runtime_capabilities.cpp
    src/platform/session/background_desktop.cpp
    src/platform/session/macos_cua.cpp
    src/platform/target/target.cpp
    src/platform/teach/recording.cpp
    src/platform/teach/event_stream.cpp
)
target_compile_features(kiseki_platform PUBLIC cxx_std_20)
target_include_directories(kiseki_platform PUBLIC src)
target_link_libraries(kiseki_platform PUBLIC kiseki_core)
if(WIN32)
    target_sources(kiseki_platform PRIVATE src/platform/capture/d3d11_capture.cpp)
    target_link_libraries(kiseki_platform PUBLIC user32 gdi32 d3d11.lib dxgi.lib uiautomationcore ole32 oleaut32)
elseif(APPLE)
    enable_language(OBJCXX)
    target_sources(kiseki_platform PRIVATE src/platform/capture/mac_capture.mm src/platform/input/mac_input.mm)
    find_library(KISEKI_APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(KISEKI_APPLICATIONSERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(KISEKI_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(KISEKI_SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit REQUIRED)
    find_library(KISEKI_COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    target_link_libraries(kiseki_platform PRIVATE
        ${KISEKI_APPLICATIONSERVICES_FRAMEWORK}
        ${KISEKI_FOUNDATION_FRAMEWORK}
        ${KISEKI_APPKIT_FRAMEWORK}
        ${KISEKI_SCREENCAPTUREKIT_FRAMEWORK}
        ${KISEKI_COREVIDEO_FRAMEWORK}
    )
    target_compile_options(kiseki_platform PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
elseif(X11_FOUND)
    target_compile_definitions(kiseki_platform PRIVATE KISEKI_HAS_X11=1)
    target_link_libraries(kiseki_platform PRIVATE X11::X11 ${CMAKE_DL_LIBS})
    if(X11_Xtst_FOUND AND EXISTS "${X11_Xtst_INCLUDE_PATH}/X11/extensions/record.h")
        target_compile_definitions(kiseki_platform PRIVATE KISEKI_HAS_XRECORD=1)
        target_link_libraries(kiseki_platform PRIVATE X11::Xtst)
    endif()
endif()
if(UNIX AND NOT APPLE AND NOT WIN32 AND KISEKI_GIO_FOUND AND KISEKI_GDK_PIXBUF_FOUND)
    target_sources(kiseki_platform PRIVATE src/platform/capture/wayland_portal_capture.cpp)
    target_compile_definitions(kiseki_platform PRIVATE KISEKI_HAS_XDG_DESKTOP_PORTAL=1)
    target_link_libraries(kiseki_platform PRIVATE PkgConfig::KISEKI_GIO PkgConfig::KISEKI_GDK_PIXBUF)
endif()
set_target_properties(kiseki_platform PROPERTIES CXX_EXTENSIONS OFF)

add_library(kiseki_webui STATIC
    src/webui/config_api.cpp
    src/webui/static_assets.cpp
    src/webui/web_server.cpp
)
target_compile_features(kiseki_webui PUBLIC cxx_std_20)
target_include_directories(kiseki_webui PUBLIC src)
target_link_libraries(kiseki_webui PRIVATE kiseki_core httplib::httplib)
set_target_properties(kiseki_webui PROPERTIES CXX_EXTENSIONS OFF)

add_library(kiseki_cli STATIC
    src/cli/app.cpp
)
target_compile_features(kiseki_cli PUBLIC cxx_std_20)
target_include_directories(kiseki_cli PUBLIC src)
target_link_libraries(kiseki_cli PRIVATE kiseki_core kiseki_platform kiseki_webui CLI11::CLI11)
set_target_properties(kiseki_cli PROPERTIES CXX_EXTENSIONS OFF)

add_executable(kiseki src/cli/main.cpp)
target_compile_features(kiseki PRIVATE cxx_std_20)
target_link_libraries(kiseki PRIVATE kiseki_cli)
set_target_properties(kiseki PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32 AND KISEKI_BUILD_IBINPUTSIMULATOR)
    get_filename_component(KISEKI_DEFAULT_IBINPUTSIMULATOR_SOURCE
        "${CMAKE_SOURCE_DIR}/../原始參考/IbInputSimulator"
        ABSOLUTE
    )
    get_filename_component(KISEKI_DEFAULT_VCPKG_ROOT
        "${CMAKE_SOURCE_DIR}/../../kiseki-vcpkg"
        ABSOLUTE
    )

    if(DEFINED ENV{VCPKG_ROOT})
        set(KISEKI_DEFAULT_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    endif()

    set(KISEKI_IBINPUTSIMULATOR_SOURCE
        "${KISEKI_DEFAULT_IBINPUTSIMULATOR_SOURCE}"
        CACHE PATH
        "Path to IbInputSimulator source tree"
    )
    set(KISEKI_VCPKG_ROOT
        "${KISEKI_DEFAULT_VCPKG_ROOT}"
        CACHE PATH
        "Path to a vcpkg tree with detours and rapidjson installed"
    )
    set(KISEKI_IBINPUTSIMULATOR_BUILD_DIR
        "${KISEKI_IBINPUTSIMULATOR_SOURCE}/build"
        CACHE PATH
        "IbInputSimulator external build directory"
    )

    set(KISEKI_IBINPUTSIMULATOR_DLL
        "${KISEKI_IBINPUTSIMULATOR_BUILD_DIR}/Simulator/Release/IbInputSimulator.dll"
    )
    set(KISEKI_VCPKG_TOOLCHAIN
        "${KISEKI_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    )
    set(KISEKI_DETOURS_INCLUDE_DIR
        "${KISEKI_VCPKG_ROOT}/installed/x64-windows-static/include"
    )
    set(KISEKI_DETOURS_RELEASE_LIBRARY
        "${KISEKI_VCPKG_ROOT}/installed/x64-windows-static/lib/detours.lib"
    )

    if(EXISTS "${KISEKI_IBINPUTSIMULATOR_SOURCE}/CMakeLists.txt"
        AND EXISTS "${KISEKI_VCPKG_TOOLCHAIN}"
        AND EXISTS "${KISEKI_DETOURS_INCLUDE_DIR}/detours/detours.h"
        AND EXISTS "${KISEKI_DETOURS_RELEASE_LIBRARY}")
        add_custom_command(
            OUTPUT "${KISEKI_IBINPUTSIMULATOR_DLL}"
            COMMAND "${CMAKE_COMMAND}"
                -S "${KISEKI_IBINPUTSIMULATOR_SOURCE}"
                -B "${KISEKI_IBINPUTSIMULATOR_BUILD_DIR}"
                -DBUILD_SHARED_LIBS=ON
                -DBUILD_TESTING=OFF
                -DCMAKE_TOOLCHAIN_FILE=${KISEKI_VCPKG_TOOLCHAIN}
                -DVCPKG_TARGET_TRIPLET=x64-windows-static
                -DDETOURS_INCLUDE_DIR:PATH=${KISEKI_DETOURS_INCLUDE_DIR}
                -DDETOURS_LIBRARY:FILEPATH=${KISEKI_DETOURS_RELEASE_LIBRARY}
            COMMAND "${CMAKE_COMMAND}"
                --build "${KISEKI_IBINPUTSIMULATOR_BUILD_DIR}"
                --config Release
            VERBATIM
        )
        add_custom_target(kiseki_ibinputsimulator ALL DEPENDS "${KISEKI_IBINPUTSIMULATOR_DLL}")
        add_dependencies(kiseki kiseki_ibinputsimulator)
        add_custom_command(TARGET kiseki POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KISEKI_IBINPUTSIMULATOR_DLL}"
                "$<TARGET_FILE_DIR:kiseki>/IbInputSimulator.dll"
            VERBATIM
        )
    else()
        message(WARNING "IbInputSimulator auto-build skipped. Expected source at ${KISEKI_IBINPUTSIMULATOR_SOURCE} and vcpkg deps at ${KISEKI_VCPKG_ROOT}.")
    endif()
endif()

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
        test/webui/config_api_test.cpp
        test/webui/static_assets_test.cpp
        test/smoke_test.cpp
        test/platform/input_sequence_test.cpp
    )

    add_executable(kiseki_tests ${KISEKI_TEST_SOURCES})
    target_compile_features(kiseki_tests PRIVATE cxx_std_20)
    target_link_libraries(kiseki_tests PRIVATE kiseki_cli kiseki_core kiseki_platform kiseki_webui Catch2::Catch2WithMain)
    target_link_libraries(kiseki_tests PRIVATE
        "$<$<PLATFORM_ID:Windows>:d3d11.lib>"
        "$<$<PLATFORM_ID:Windows>:dxgi.lib>"
    )
    set_target_properties(kiseki_tests PROPERTIES CXX_EXTENSIONS OFF)
    catch_discover_tests(kiseki_tests)
endif()

# Opt-in: this executable opens its own GUI window and exercises real input.
# It is deliberately not registered with unattended CTest runs.
option(KISEKI_BUILD_NATIVE_INPUT_PROBE "Build the interactive native input regression probe" OFF)
if(KISEKI_BUILD_NATIVE_INPUT_PROBE)
    if(APPLE)
        add_executable(kiseki_native_input_probe test/native/input_probe.mm)
        target_compile_options(kiseki_native_input_probe PRIVATE -fobjc-arc)
        target_link_libraries(kiseki_native_input_probe PRIVATE ${KISEKI_APPKIT_FRAMEWORK})
    else()
        add_executable(kiseki_native_input_probe test/native/input_probe.cpp)
        if(UNIX)
            target_link_libraries(kiseki_native_input_probe PRIVATE X11::X11)
        endif()
    endif()
    target_compile_features(kiseki_native_input_probe PRIVATE cxx_std_20)
    target_link_libraries(kiseki_native_input_probe PRIVATE kiseki_cli kiseki_platform)
endif()

if(WIN32 AND BUILD_TESTING AND KISEKI_BUILD_TESTING)
    add_executable(kiseki_windows_input_fault_test
        test/platform/windows_input_fault_test.cpp
        src/platform/input/sequence_support.cpp
        src/platform/target/target.cpp)
    target_compile_features(kiseki_windows_input_fault_test PRIVATE cxx_std_20)
    target_include_directories(kiseki_windows_input_fault_test PRIVATE src)
    target_link_libraries(kiseki_windows_input_fault_test PRIVATE user32)
    add_test(NAME windows_input_boundary_faults COMMAND kiseki_windows_input_fault_test)
endif()

if(APPLE AND BUILD_TESTING AND KISEKI_BUILD_TESTING)
    add_executable(kiseki_macos_input_fault_test
        test/platform/macos_input_fault_test.cpp
        src/platform/input/sequence_support.cpp
        src/platform/input/mac_input.mm
        src/platform/target/target.cpp)
    target_compile_features(kiseki_macos_input_fault_test PRIVATE cxx_std_20)
    target_include_directories(kiseki_macos_input_fault_test PRIVATE src)
    target_link_libraries(kiseki_macos_input_fault_test PRIVATE
        ${KISEKI_APPLICATIONSERVICES_FRAMEWORK} ${KISEKI_APPKIT_FRAMEWORK} ${KISEKI_FOUNDATION_FRAMEWORK})
    add_test(NAME macos_input_delayed_boundary COMMAND kiseki_macos_input_fault_test)
endif()
