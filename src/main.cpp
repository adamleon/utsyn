#include "app/Application.hpp"

#include <string>

// Entry point. Construct the Application and run the render loop.
//
// `--vulkan` selects the experimental threepp Vulkan deferred-hybrid backend
// (requires a build with UTSYN_WITH_VULKAN). The common typo `--vulcan` is
// accepted too. Default is OpenGL.
int main(int argc, char** argv) {
    bool useVulkan = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--vulkan" || arg == "--vulcan") {
            useVulkan = true;
        }
    }

    utsyn::Application app;
    app.run(useVulkan);
    return 0;
}
