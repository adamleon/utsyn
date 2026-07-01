#include "TfTree.hpp"

#include "app/Theme.hpp"
#include "ros/TfListener.hpp"
#include "widgets/TerminalUi.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

namespace utsyn {

namespace {

constexpr int    kNameCol = 26;                        // width (chars) of the name column
constexpr double kRadToDeg = 57.29577951308232;        // 180/pi

// Quaternion (x,y,z,w) -> intrinsic roll/pitch/yaw (X-Y-Z) in degrees, for a readable,
// live-updating rotation display (the part that actually changes as a revolute joint moves).
void quatToRpyDeg(const TfTransform& t, double& roll, double& pitch, double& yaw) {
    const double sinrCosp = 2.0 * (t.qw * t.qx + t.qy * t.qz);
    const double cosrCosp = 1.0 - 2.0 * (t.qx * t.qx + t.qy * t.qy);
    roll                  = std::atan2(sinrCosp, cosrCosp) * kRadToDeg;

    const double sinp = 2.0 * (t.qw * t.qy - t.qz * t.qx);
    pitch = (std::abs(sinp) >= 1.0 ? std::copysign(1.5707963267948966, sinp) : std::asin(sinp)) *
            kRadToDeg;

    const double sinyCosp = 2.0 * (t.qw * t.qz + t.qx * t.qy);
    const double cosyCosp = 1.0 - 2.0 * (t.qy * t.qy + t.qz * t.qz);
    yaw                   = std::atan2(sinyCosp, cosyCosp) * kRadToDeg;
}

// Rotate the vector (vx,vy,vz) by q's quaternion (only q's rotation fields are read).
void rotateByQuat(const TfTransform& q, double vx, double vy, double vz, double& ox, double& oy,
                  double& oz) {
    const double tx = 2.0 * (q.qy * vz - q.qz * vy);
    const double ty = 2.0 * (q.qz * vx - q.qx * vz);
    const double tz = 2.0 * (q.qx * vy - q.qy * vx);
    ox              = vx + q.qw * tx + (q.qy * tz - q.qz * ty);
    oy              = vy + q.qw * ty + (q.qz * tx - q.qx * tz);
    oz              = vz + q.qw * tz + (q.qx * ty - q.qy * tx);
}

// Compose transform a (world->parent) with b (parent->child) => world->child.
TfTransform compose(const TfTransform& a, const TfTransform& b) {
    TfTransform c;
    // rotation: c.q = a.q * b.q
    c.qw = a.qw * b.qw - a.qx * b.qx - a.qy * b.qy - a.qz * b.qz;
    c.qx = a.qw * b.qx + a.qx * b.qw + a.qy * b.qz - a.qz * b.qy;
    c.qy = a.qw * b.qy - a.qx * b.qz + a.qy * b.qw + a.qz * b.qx;
    c.qz = a.qw * b.qz + a.qx * b.qy - a.qy * b.qx + a.qz * b.qw;
    // translation: c.t = a.t + rotate(a.q, b.t)
    double rx = 0.0, ry = 0.0, rz = 0.0;
    rotateByQuat(a, b.tx, b.ty, b.tz, rx, ry, rz);
    c.tx = a.tx + rx;
    c.ty = a.ty + ry;
    c.tz = a.tz + rz;
    return c;
}

// Recursively render `name` and its children as ASCII/terminal rows: an indented
// [+]/[-] marker + name in a fixed-width column, then monospace-aligned x/y/z. Green =
// dynamic frame, grey = static; roots without a transform (e.g. "world") stay neutral.
void drawFrameRow(const TfListener::Snapshot& snap, const std::string& name, int depth,
                  std::set<std::string>& collapsed, bool absolute, const TfTransform& parentWorld) {
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
    // Accumulate this frame's world pose so children can compose from it and absolute
    // mode can display it (parentWorld is identity at the roots).
    TfTransform world = parentWorld;
    char        row[256];
    if (hasTf) {
        world                   = compose(parentWorld, frameIt->second.transform);
        const TfTransform& t    = absolute ? world : frameIt->second.transform;
        double             roll = 0.0, pitch = 0.0, yaw = 0.0;
        quatToRpyDeg(t, roll, pitch, yaw);
        std::snprintf(row, sizeof(row), "%-*s %6.2f %6.2f %6.2f %6.1f %6.1f %6.1f###%s", kNameCol,
                      left, t.tx, t.ty, t.tz, roll, pitch, yaw, name.c_str());
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
        const TfTransform& t = absolute ? world : frameIt->second.transform;
        ImGui::SetTooltip("parent: %s\n%s quat: %.3f %.3f %.3f %.3f%s",
                          frameIt->second.parent.c_str(), absolute ? "world" : "rel", t.qx, t.qy,
                          t.qz, t.qw, isStatic ? "\nstatic" : "");
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
            drawFrameRow(snap, child, depth + 1, collapsed, absolute, world);
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

    ImGui::Checkbox("absolute (world frame)", &absolute_);
    ImGui::TextDisabled("%llu transform(s)   %s   xyz = m, rpy = deg",
                        static_cast<unsigned long long>(snap.frames.size()),
                        absolute_ ? "world-absolute" : "relative to parent");
    char header[160];
    std::snprintf(header, sizeof(header), "%-*s %6s %6s %6s %6s %6s %6s", kNameCol, "frame", "x",
                  "y", "z", "roll", "pitch", "yaw");
    ImGui::TextDisabled("%s", header);
    ui::dashRule();

    const TfTransform identity{}; // world -> root
    for (const auto& root : snap.roots) {
        drawFrameRow(snap, root, 0, collapsed_, absolute_, identity);
    }
    ImGui::End();
}

} // namespace utsyn
