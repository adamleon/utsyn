#include "TfTree.hpp"

#include "app/Theme.hpp"
#include "ros/TfListener.hpp"
#include "widgets/TerminalUi.hpp"

#include <imgui.h>

#include <cstdio>
#include <set>
#include <string>

namespace utsyn {

namespace {

constexpr int kNameCol = 30; // width (chars) of the indent+marker+name column

// Recursively render `name` and its children as ASCII/terminal rows: an indented
// [+]/[-] marker + name in a fixed-width column, then monospace-aligned x/y/z. Green =
// dynamic frame, grey = static; roots without a transform (e.g. "world") stay neutral.
void drawFrameRow(const TfListener::Snapshot& snap, const std::string& name, int depth,
                  std::set<std::string>& collapsed) {
    const auto childrenIt  = snap.children.find(name);
    const bool hasChildren = childrenIt != snap.children.end() && !childrenIt->second.empty();
    const auto frameIt     = snap.frames.find(name);
    const bool hasTf       = frameIt != snap.frames.end();
    const bool isStatic    = hasTf && frameIt->second.isStatic;
    bool       expanded    = collapsed.find(name) == collapsed.end();

    // Indent (2 spaces/level) + [+]/[-] for a parent, blanks for a leaf, + the name.
    const char* marker = hasChildren ? (expanded ? "[-]" : "[+]") : "   ";
    char        left[128];
    std::snprintf(left, sizeof(left), "%*s%s %s", depth * 2, "", marker, name.c_str());

    // One full-width row string. "###name" is a stable id so the text can change each
    // frame (live transforms) without the row losing its click/hover identity.
    char row[224];
    if (hasTf) {
        const TfTransform& t = frameIt->second.transform;
        std::snprintf(row, sizeof(row), "%-*s %7.2f %7.2f %7.2f###%s", kNameCol, left, t.tx, t.ty,
                      t.tz, name.c_str());
    } else {
        std::snprintf(row, sizeof(row), "%-*s###%s", kNameCol, left, name.c_str());
    }

    const bool colored = hasTf;
    if (colored) {
        ImGui::PushStyleColor(ImGuiCol_Text, isStatic ? theme::StatusIdle : theme::StatusLive);
    }
    const bool clicked = ImGui::Selectable(row, false, ImGuiSelectableFlags_SpanAllColumns);
    if (colored) {
        ImGui::PopStyleColor();
    }

    if (hasTf && ImGui::IsItemHovered()) {
        const TfFrame& f = frameIt->second;
        ImGui::SetTooltip("parent: %s\nquat:   %.3f %.3f %.3f %.3f%s", f.parent.c_str(),
                          f.transform.qx, f.transform.qy, f.transform.qz, f.transform.qw,
                          isStatic ? "\nstatic" : "");
    }
    if (clicked && hasChildren) {
        if (expanded) {
            collapsed.insert(name);
        } else {
            collapsed.erase(name);
        }
        expanded = !expanded;
    }

    if (expanded && hasChildren) {
        for (const auto& child : childrenIt->second) {
            drawFrameRow(snap, child, depth + 1, collapsed);
        }
    }
}

} // namespace

void TfTree::onImGui(const TfListener& tf, bool* pOpen) {
    if (!ImGui::Begin("TF Tree", pOpen)) {
        ImGui::End();
        return;
    }

    const TfListener::Snapshot snap = tf.snapshot();
    if (snap.roots.empty()) {
        ImGui::TextDisabled("No transforms.");
        ImGui::TextDisabled("(/tf + /tf_static populate this when ROS2 is on.)");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("%llu transform(s)  [-]/[+] to collapse",
                        static_cast<unsigned long long>(snap.frames.size()));
    char header[128];
    std::snprintf(header, sizeof(header), "%-*s %7s %7s %7s", kNameCol, "frame", "x", "y", "z");
    ImGui::TextDisabled("%s", header);
    ui::dashRule();

    for (const auto& root : snap.roots) {
        drawFrameRow(snap, root, 0, collapsed_);
    }
    ImGui::End();
}

} // namespace utsyn
