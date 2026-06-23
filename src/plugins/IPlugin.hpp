#pragma once
#include <functional>
#include <string>

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
