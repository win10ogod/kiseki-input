#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kiseki::platform::target {

struct TargetQuery {
    std::string title;
    std::uint32_t pid = 0;
    std::string window_id;
};

struct TargetWindow {
    std::string id;
    std::string title;
    std::uint32_t pid = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ResolveResult {
    bool ok = false;
    int code = 2;
    TargetWindow window;
    std::string error;
};

struct ListResult {
    bool ok = false;
    int code = 2;
    std::vector<TargetWindow> windows;
    std::string error;
};

bool has_target_selector(const TargetQuery& query);
bool target_window_available();
ListResult list_windows(const TargetQuery& filter = {});
ResolveResult resolve_window(const TargetQuery& query);

}
