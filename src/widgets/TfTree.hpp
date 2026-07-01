#pragma once

#include <set>
#include <string>

namespace utsyn {

class TfListener;

// ImGui panel showing the TF frame tree (from a TfListener) as an ASCII/terminal-style
// hierarchy: [+]/[-] collapse markers, monospace-aligned x/y/z columns, dash-rule header.
class TfTree {
public:
    // Draw the panel between ImGui::NewFrame and ImGui::Render. pOpen drives the window's
    // close button (nullptr = no close button).
    void onImGui(const TfListener& tf, bool* pOpen = nullptr);

private:
    // Frames the user has collapsed (default state is expanded, i.e. not in the set).
    std::set<std::string> collapsed_;
    // false: each frame relative to its parent (raw /tf). true: composed to the world/root
    // frame (absolute pose).
    bool absolute_ = false;
};

} // namespace utsyn
