// ExamplePlugin — reference IPlugin implementation.
//
// Demonstrates the full plugin contract: implement IPlugin, hold the
// PluginContext, draw an ImGui panel each frame, and export the C entry
// points the PluginLoader resolves by name.
//
// The broker/scene/viewport/logger collaborators are still stubs (their
// methods are TODOs), so this plugin only touches what currently exists —
// ImGui — and marks with TODO where the real APIs will plug in later.

#include "plugins/IPlugin.hpp"

#include <imgui.h>

#include <string>

namespace utsyn {

class ExamplePlugin final : public IPlugin {
public:
    void initialize(PluginContext& ctx) override {
        ctx_ = &ctx;
        // TODO: subscribe to a topic once SubscriptionBroker::subscribe exists:
        //   ctx.broker.subscribe<sensor_msgs::msg::JointState>(
        //       "/joint_states", [this](const auto& msg) { onJointState(msg); });
        // TODO: ctx.logger.info("ExamplePlugin initialized");
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
        // TODO: ctx_->logger.info("ExamplePlugin shutting down");
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
