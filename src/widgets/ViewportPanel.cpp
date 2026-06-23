#include "ViewportPanel.hpp"

#include "rendering/Viewport.hpp"

#include <imgui.h>

#include <utility>

namespace utsyn {

ViewportPanel::ViewportPanel(std::string title, Viewport& viewport)
    : title_(std::move(title)), viewport_(viewport) {}

void ViewportPanel::onImGui(threepp::GLRenderer& renderer) {
    // No window padding so the 3D image fills the panel edge to edge.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin(title_.c_str());
    ImGui::PopStyleVar();

    if (open) {
        // `avail` is in ImGui logical points; the framebuffer may be at a
        // higher pixel density (HiDPI). Render the offscreen target at the
        // actual pixel size so the image is crisp and its aspect matches the
        // panel, but display it at the logical size.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
        const int pw = static_cast<int>(avail.x * fbScale.x);
        const int ph = static_cast<int>(avail.y * fbScale.y);
        if (pw > 0 && ph > 0) {
            viewport_.resize(pw, ph);
            viewport_.render(renderer);
            const unsigned int tex = viewport_.textureId(renderer);
            if (tex != 0) {
                // GL textures are bottom-up; flip V (uv0.y=1, uv1.y=0) so the
                // image is displayed upright.
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::Image(static_cast<ImTextureID>(tex), avail,
                             ImVec2(0, 1), ImVec2(1, 0));

                // Transparent button over the image to capture navigation
                // input. Stays "active" while dragging even if the cursor
                // leaves the panel.
                ImGui::SetCursorScreenPos(origin);
                ImGui::InvisibleButton("##viewport_input", avail,
                                       ImGuiButtonFlags_MouseButtonLeft |
                                               ImGuiButtonFlags_MouseButtonRight |
                                               ImGuiButtonFlags_MouseButtonMiddle);
                handleNavigation();
            }
        }
    }
    ImGui::End();
}

void ViewportPanel::handleNavigation() {
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();

    if (active) {
        const ImVec2 d = io.MouseDelta;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Left drag: orbit. ~0.01 rad/pixel.
            viewport_.orbit(-d.x * 0.01f, -d.y * 0.01f);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                   ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            // Right/middle drag: pan the target.
            viewport_.pan(d.x, d.y);
        }
    }

    if (hovered && io.MouseWheel != 0.0f) {
        viewport_.dolly(io.MouseWheel); // wheel: zoom
    }
}

} // namespace utsyn
