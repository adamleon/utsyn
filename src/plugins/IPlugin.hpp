#pragma once
#include <functional>
#include <string>

// Plugin ABI version. Bump this whenever a change would break already-compiled
// plugins: the IPlugin vtable, the PluginContext layout/fields, or the
// ISubscriptionRegistry interface. PluginLoader resolves each plugin's exported
// utsynPluginAbiVersion() and refuses to load a plugin whose value differs.
//
// NOTE: this version gate does NOT cover toolchain/CRT mismatch. std::string,
// std::function and std::shared_ptr cross the plugin<->utsyn_core boundary, so
// every plugin MUST be built with the same compiler, C runtime (/MD[d]) and build
// config (_ITERATOR_DEBUG_LEVEL) as utsyn_core. A Debug-plugin / Release-core mix
// is unsupported and will corrupt at runtime even when the version matches.
#define UTSYN_PLUGIN_ABI_VERSION 1

namespace utsyn {

class SceneManager;
class SubscriptionBroker;
class ViewportManager;
class Logger;

struct PluginContext {
    SubscriptionBroker& broker;    // Request topic subscriptions through this — never use rclcpp directly
    SceneManager&       scene;     // Add/remove 3D scene objects
    ViewportManager&    viewports; // Create or reference viewports
    Logger&             logger;    // Log messages
    // Config store for plugin-specific persistent settings — TBD
};

class IPlugin {
public:
    virtual ~IPlugin() = default;

    // Called once after loading
    virtual void initialize(PluginContext& ctx) = 0;

    // Called every frame — render ImGui panels here
    // Must be fast and non-blocking
    virtual void onImGui() = 0;

    // Called every frame — update threepp scene objects here
    virtual void onSceneUpdate(float deltaTime) = 0;

    // Called before unloading
    virtual void shutdown() = 0;

    // Human-readable name shown in UI
    virtual std::string name() const = 0;

    // Semantic version string e.g. "1.0.0"
    virtual std::string version() const = 0;
};

} // namespace utsyn

// Each plugin .so/.dll must export:
// extern "C" utsyn::IPlugin* createPlugin();
// extern "C" void destroyPlugin(utsyn::IPlugin*);
// extern "C" int  utsynPluginAbiVersion();   // return UTSYN_PLUGIN_ABI_VERSION
