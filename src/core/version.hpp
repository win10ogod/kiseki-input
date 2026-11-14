#pragma once

#include <string>
#include <string_view>

namespace kiseki::core {

// Semantic version of this build, for example "0.1.0".
std::string_view version();

// Git commit recorded when this binary was built, or "unknown".
std::string_view build_commit();

enum class BuildTreeState {
    clean,
    dirty,
    unknown,
};

BuildTreeState build_tree_state();
std::string_view build_tree_state_name(BuildTreeState state);

// `kiseki --version` text. The first token is always the semantic version.
std::string version_text();

}
