#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace utsyn {

class IPlugin;
struct PluginContext;

// Loads plugin shared libraries (plugin_*.dll / plugin_*.so), resolves their C
// entry points, version-checks them, and drives their lifecycle. Teardown order
// is load-bearing — see Application::shutdown(): stop ROS, clear the broker, THEN
// shutdownAll()/unloadAll() here, so no callback or captured closure touches a
// freed plugin DLL.
class PluginLoader {
public:
    PluginLoader() = default;
    ~PluginLoader();

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // Load and initialize() every plugin_*.{dll,so} in `dir`. Returns the count
    // successfully loaded.
    std::size_t loadDirectory(const std::filesystem::path& dir, PluginContext& ctx);

    // Render-thread per-frame fan-out.
    void onSceneUpdate(float deltaTime);
    void onImGui();

    [[nodiscard]] std::size_t count() const noexcept { return loaded_.size(); }

    // Teardown step 1: shutdown() each plugin (reverse load order). Idempotent.
    void shutdownAll() noexcept;
    // Teardown step 2: destroyPlugin() + unload each library (reverse). Idempotent.
    void unloadAll() noexcept;

    // Directory of the running executable — where plugin DLLs are emitted.
    [[nodiscard]] static std::filesystem::path executableDir();

private:
    struct Loaded {
        std::string path;
        void*       handle = nullptr;          // HMODULE / dlopen handle
        IPlugin*    plugin = nullptr;
        void (*destroy)(IPlugin*) = nullptr;
        bool        shutDown = false;
    };

    IPlugin* loadOne(const std::filesystem::path& file, PluginContext& ctx);

    std::vector<Loaded> loaded_;
};

} // namespace utsyn
