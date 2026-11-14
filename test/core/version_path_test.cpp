#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "core/utf8_path.hpp"
#include "core/version.hpp"

TEST_CASE("version text starts with the semantic version and names the build commit") {
    const auto text = kiseki::core::version_text();
    const std::string expected_prefix = std::string{kiseki::core::version()} + " (commit ";
    REQUIRE(text.rfind(expected_prefix, 0) == 0);
    REQUIRE(text.back() == ')');
    REQUIRE(text.find(std::string{kiseki::core::build_commit()}) != std::string::npos);

    const auto commit = kiseki::core::build_commit();
    REQUIRE_FALSE(commit.empty());
    if (commit != "unknown") {
        REQUIRE(commit.find_first_not_of("0123456789abcdef") == std::string_view::npos);
    } else {
        REQUIRE(kiseki::core::build_tree_state() == kiseki::core::BuildTreeState::unknown);
    }
    const auto state = kiseki::core::build_tree_state_name(kiseki::core::build_tree_state());
    REQUIRE((state == "clean" || state == "dirty" || state == "unknown"));
}

TEST_CASE("UTF-8 path helper round-trips non-ASCII names on every platform") {
    const std::string name = "kiseki-\xE8\xBC\x9D\xE8\x89\xB2-\xC3\xA9.txt"; // U+8F1D U+8272 U+00E9
    const auto path = kiseki::core::path_from_utf8(name);
    REQUIRE(kiseki::core::path_to_utf8(path) == name);
    REQUIRE(path.filename().u8string() == std::u8string{u8"kiseki-\u8F1D\u8272-\u00E9.txt"});

    const auto directory = std::filesystem::temp_directory_path() / "kiseki-utf8-path-test";
    std::filesystem::create_directories(directory);
    const auto file = directory / path;
    {
        std::ofstream output{file, std::ios::binary};
        output << "x";
    }
    REQUIRE(std::filesystem::exists(kiseki::core::path_from_utf8(kiseki::core::path_to_utf8(file))));
    std::filesystem::remove_all(directory);
}
