#pragma once

#include <imgui.h>

// Named status colors for the terminal aesthetic. ImGuiStyle has no custom color
// slots, so these live here as the single source of truth and are applied with a
// transient PushStyleColor(ImGuiCol_Text, ...) where status is shown (e.g. the
// MessageMonitor). Green stays the accent; amber/red only ever signal stream
// health, never general UI.
namespace utsyn::theme {

inline const ImVec4 StatusLive {0.20f, 0.80f, 0.20f, 1.0f}; // ~#33CC33 accent green
inline const ImVec4 StatusStale{0.80f, 0.667f, 0.20f, 1.0f}; // #CCAA33 amber
inline const ImVec4 StatusError{0.80f, 0.267f, 0.20f, 1.0f}; // #CC4433 muted red
inline const ImVec4 StatusIdle {0.50f, 0.50f, 0.50f, 1.0f}; // grey — waiting / disabled

} // namespace utsyn::theme
