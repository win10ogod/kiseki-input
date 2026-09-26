#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "platform/notification/mac_notification.hpp"

TEST_CASE("macOS notification keeps the message outside AppleScript source") {
    const std::string message = "-e quoted \"message\" 'single' $HOME $(whoami) 提醒";

    const auto arguments = kiseki::platform::notification::detail::macos_notification_process_arguments(message);
    REQUIRE(arguments == std::vector<std::string>{"-", message});

    const auto script = kiseki::platform::notification::detail::macos_notification_script();
    REQUIRE(script.find("display notification") != std::string_view::npos);
    REQUIRE(script.find("Kiseki Input") != std::string_view::npos);
    REQUIRE(script.find(message) == std::string_view::npos);
}
