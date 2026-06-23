// ExamplePlugin — reference IPlugin implementation.
//
// Demonstrates the full plugin contract: implement IPlugin, hold the
// PluginContext, draw an ImGui panel each frame, and export the C entry
// points the PluginLoader resolves by name.
//
// The broker/scene/viewport collaborators now exist; this reference plugin wires
// the lifecycle and the Logger, and draws an ImGui panel. Topic monitoring lands
// in the dedicated demo plugin (the MessageMonitor widget, next deliverable).

#include "app/Logger.hpp"
#include "plugins/IPlugin.hpp"

#include <imgui.h>

#include <string>

namespace utsyn {

class ExamplePlugin final : public IPlugin {
public:
    void initialize(PluginContext& ctx) override {
        ctx_ = &ctx;
        ctx.logger.info("ExamplePlugin: initialized");
        // TODO: subscribe to a topic once the MessageMonitor widget lands:
        //   ctx.broker.subscribe<sensor_msgs::msg::JointState>(
        //       "/joint_states", [this](const auto& msg) { onJointState(msg); });
    }

    void onImGui() override {
        if (!panelOpen_) {
            return;
        }
        if (ImGui::Begin("Example Plugin", &panelOpen_)) {
            ImGui::TextUnformatted("Reference plugin online.");
            ImGui::Separator();
            ImGui::Text("Uptime: %.1f s", uptimeSeconds_);
            ImGui::Text("Frames: %llu",
                        static_cast<unsigned long long>(frameCount_));
        }
        ImGui::End();
    }

    void onSceneUpdate(float deltaTime) override {
        uptimeSeconds_ += deltaTime;
        ++frameCount_;
        // TODO: drive scene objects via ctx_->scene once SceneManager has an API.
    }

    void shutdown() override {
        if (ctx_) {
            ctx_->logger.info("ExamplePlugin: shutting down");
        }
        ctx_ = nullptr;
    }

    std::string name() const override { return "Example Plugin"; }
    std::string version() const override { return "1.0.0"; }

private:
    PluginContext* ctx_ = nullptr;
    float          uptimeSeconds_ = 0.0f;
    unsigned long  frameCount_ = 0;
    bool           panelOpen_ = true;
};

} // namespace utsyn

// --- Plugin entry points ---------------------------------------------------
// Exported so PluginLoader can resolve them from the loaded .dll/.so.

#if defined(_WIN32)
#  define UTSYN_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define UTSYN_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" UTSYN_PLUGIN_EXPORT utsyn::IPlugin* createPlugin() {
    return new utsyn::ExamplePlugin();
}

extern "C" UTSYN_PLUGIN_EXPORT void destroyPlugin(utsyn::IPlugin* plugin) {
    delete plugin;
}

extern "C" UTSYN_PLUGIN_EXPORT int utsynPluginAbiVersion() {
    return UTSYN_PLUGIN_ABI_VERSION;
}
