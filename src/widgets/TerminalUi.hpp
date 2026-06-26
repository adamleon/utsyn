#pragma once

#include <imgui.h>

#include <string>

// Small terminal-aesthetic ImGui helpers shared by the app and plugins. Header-only
// (inline) so they instantiate wherever included; they only call exported ImGui
// functions, so they work across the plugin DLL boundary.
namespace utsyn::ui {

// Full-width horizontal rule made of repeated `fill` characters — exactly one text
// line tall, unlike ImGui::Separator() (a 1px sub-line rule). The terminal divider.
inline void dashRule(char fill = '-') {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float charW = ImGui::CalcTextSize("-").x;
    const int   count = (charW > 0.0f) ? static_cast<int>(avail / charW) : 0;
    ImGui::TextUnformatted(
            std::string(static_cast<std::size_t>(count > 0 ? count : 1), fill).c_str());
}

// Terminal-style collapsible header: an ASCII [+]/[-] marker instead of ImGui's
// vector triangle, drawn as a full-width clickable row. `open` is owned by the
// caller and toggled on click. `label` must end with "###<stable-id>" so the
// visible text can change each frame without losing the open/closed state. Wrap in
// PushStyleColor(ImGuiCol_Text, ...) to color the whole row.
inline bool collapsibleRow(bool& open, const char* label) {
    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns)) {
        open = !open;
    }
    return open;
}

// The ASCII marker for a collapse state, e.g. to compose into a header string.
inline const char* collapseMarker(bool open) { return open ? "[-]" : "[+]"; }

} // namespace utsyn::ui
