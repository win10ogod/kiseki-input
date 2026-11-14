#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// Platform-independent selection of X11 target windows. The X11 backend supplies
// tree/property callbacks; tests supply a fake tree.
namespace kiseki::platform::target::detail {

using X11WindowId = unsigned long;

struct X11TreeAccess {
    std::function<std::vector<X11WindowId>(X11WindowId)> children;
    std::function<bool(X11WindowId)> has_wm_state;
    // Mapped, InputOutput, not override-redirect: a plausible top-level window without a window manager.
    std::function<bool(X11WindowId)> is_unmanaged_top_level;
};

// Like XmuClientWindow: the window itself when it has WM_STATE, otherwise the
// nearest descendant (breadth-first) with WM_STATE.
inline std::optional<X11WindowId> find_x11_client_window(
    X11WindowId top_level,
    const X11TreeAccess& access,
    std::size_t max_visited = 4096) {
    std::deque<X11WindowId> pending{top_level};
    std::size_t visited = 0;
    while (!pending.empty() && visited < max_visited) {
        const X11WindowId window = pending.front();
        pending.pop_front();
        ++visited;
        if (access.has_wm_state(window)) {
            return window;
        }
        for (const X11WindowId child : access.children(window)) {
            pending.push_back(child);
        }
    }
    return std::nullopt;
}

struct X11TargetSelection {
    std::vector<X11WindowId> windows;
    // "net-client-list", "wm-state", or "top-level" (no window manager detected).
    const char* source = "top-level";
};

// Target windows are application top-level (client) windows, not their internal
// widget subwindows or window-manager frames:
//   1. _NET_CLIENT_LIST when an EWMH window manager is verified to be running;
//   2. otherwise clients marked with WM_STATE below each root child;
//   3. otherwise (no window manager, e.g. a bare Xvfb) mapped top-level root children.
inline X11TargetSelection select_x11_target_windows(
    X11WindowId root,
    const std::optional<std::vector<X11WindowId>>& verified_net_client_list,
    const X11TreeAccess& access) {
    if (verified_net_client_list) {
        return X11TargetSelection{*verified_net_client_list, "net-client-list"};
    }

    const auto top_levels = access.children(root);
    X11TargetSelection selection{{}, "wm-state"};
    for (const X11WindowId top_level : top_levels) {
        if (const auto client = find_x11_client_window(top_level, access)) {
            selection.windows.push_back(*client);
        }
    }
    if (!selection.windows.empty()) {
        return selection;
    }

    selection.source = "top-level";
    for (const X11WindowId top_level : top_levels) {
        if (access.is_unmanaged_top_level(top_level)) {
            selection.windows.push_back(top_level);
        }
    }
    return selection;
}

}
