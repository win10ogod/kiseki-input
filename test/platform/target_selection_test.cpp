#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <set>
#include <vector>

#include "platform/target/target.hpp"
#include "platform/target/x11_client_windows.hpp"

namespace {

using kiseki::platform::target::detail::X11TreeAccess;
using kiseki::platform::target::detail::X11WindowId;

struct FakeTree {
    std::map<X11WindowId, std::vector<X11WindowId>> children;
    std::set<X11WindowId> wm_state;
    std::set<X11WindowId> unmanaged_top_levels;

    X11TreeAccess access() const {
        return X11TreeAccess{
            .children = [this](X11WindowId window) {
                const auto found = children.find(window);
                return found == children.end() ? std::vector<X11WindowId>{} : found->second;
            },
            .has_wm_state = [this](X11WindowId window) { return wm_state.count(window) != 0; },
            .is_unmanaged_top_level = [this](X11WindowId window) { return unmanaged_top_levels.count(window) != 0; },
        };
    }
};

constexpr X11WindowId root = 1;

}

TEST_CASE("X11 targets without a window manager are top-level windows, not widget subwindows") {
    // One xmessage-like client: a top-level window with four internal widget windows.
    FakeTree tree;
    tree.children[root] = {0x200001, 0x300001};
    tree.children[0x200001] = {0x200002, 0x200003};
    tree.children[0x200003] = {0x200004, 0x200005};
    tree.unmanaged_top_levels = {0x200001}; // 0x300001 is an unmapped or override-redirect window

    const auto selection = kiseki::platform::target::detail::select_x11_target_windows(root, std::nullopt, tree.access());
    REQUIRE(selection.windows == std::vector<X11WindowId>{0x200001});
    REQUIRE(std::string{selection.source} == "top-level");
}

TEST_CASE("X11 targets under a reparenting window manager are WM_STATE clients") {
    FakeTree tree;
    tree.children[root] = {0x100001, 0x100010, 0x500000};
    tree.children[0x100001] = {0x100002};          // frame -> client
    tree.children[0x100002] = {0x100003};          // client's widget child
    tree.children[0x100010] = {0x100011};          // frame -> decoration
    tree.children[0x100011] = {0x100012};          // decoration -> client
    tree.wm_state = {0x100002, 0x100012};
    tree.unmanaged_top_levels = {0x500000};        // an unmanaged window is not listed once clients exist

    const auto selection = kiseki::platform::target::detail::select_x11_target_windows(root, std::nullopt, tree.access());
    REQUIRE(selection.windows == std::vector<X11WindowId>{0x100002, 0x100012});
    REQUIRE(std::string{selection.source} == "wm-state");
}

TEST_CASE("X11 targets prefer a verified _NET_CLIENT_LIST") {
    FakeTree tree;
    tree.children[root] = {0x100001};
    tree.wm_state = {0x100001};
    const std::vector<X11WindowId> clients{0x400001, 0x400100};

    const auto selection = kiseki::platform::target::detail::select_x11_target_windows(root, clients, tree.access());
    REQUIRE(selection.windows == clients);
    REQUIRE(std::string{selection.source} == "net-client-list");

    const auto empty = kiseki::platform::target::detail::select_x11_target_windows(
        root, std::vector<X11WindowId>{}, tree.access());
    REQUIRE(empty.windows.empty());
}

TEST_CASE("window ids parse as hexadecimal with 0x and decimal otherwise") {
    using kiseki::platform::target::window_id_value;
    REQUIRE(window_id_value("0x340656") == 0x340656U);
    REQUIRE(window_id_value("0X340656") == 0x340656U);
    REQUIRE(window_id_value("3409494") == 3409494U);
    REQUIRE(window_id_value("0123") == 123U); // decimal, not octal
    REQUIRE_FALSE(window_id_value("").has_value());
    REQUIRE_FALSE(window_id_value("0x").has_value());
    REQUIRE_FALSE(window_id_value("12ab").has_value());
    REQUIRE_FALSE(window_id_value("-5").has_value());
    REQUIRE_FALSE(window_id_value("0x12 ").has_value());
}
