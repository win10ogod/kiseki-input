#include "core/version.hpp"

#include <string>

#include "kiseki_build_info.hpp"

namespace kiseki::core {

std::string_view version() {
    return KISEKI_VERSION;
}

std::string_view build_commit() {
    return KISEKI_BUILD_COMMIT;
}

BuildTreeState build_tree_state() {
    if (build_commit() == "unknown" || KISEKI_BUILD_DIRTY == 2) {
        return BuildTreeState::unknown;
    }
    return KISEKI_BUILD_DIRTY == 1 ? BuildTreeState::dirty : BuildTreeState::clean;
}

std::string_view build_tree_state_name(BuildTreeState state) {
    switch (state) {
    case BuildTreeState::clean:
        return "clean";
    case BuildTreeState::dirty:
        return "dirty";
    case BuildTreeState::unknown:
        break;
    }
    return "unknown";
}

std::string version_text() {
    std::string text{version()};
    text += " (commit ";
    text += build_commit();
    const auto state = build_tree_state();
    if (state == BuildTreeState::dirty) {
        text += ", dirty";
    } else if (state == BuildTreeState::unknown && build_commit() != "unknown") {
        text += ", tree state unknown";
    }
    text += ')';
    return text;
}

}
