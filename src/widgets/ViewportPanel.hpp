#pragma once

#include <string>

namespace threepp {
class GLRenderer;
} // namespace threepp

namespace utsyn {

class Viewport;

// ImGui panel hosting a threepp Viewport, embedded as a render texture.
class ViewportPanel {
public:
    ViewportPanel(std::string title, Viewport& viewport);

    // Render the panel. Renders the viewport's scene offscreen (via renderer)
    // and displays the result. Call between ImGui::NewFrame and ImGui::Render.
    void onImGui(threepp::GLRenderer& renderer);

private:
    // Feed ImGui mouse input to the viewport camera. Call immediately after the
    // input InvisibleButton is submitted (queries that item's active state).
    void handleNavigation();

    std::string title_;
    Viewport&   viewport_;
};

} // namespace utsyn
