#include "plugins/PluginLoader.hpp"

#include "app/Logger.hpp"
#include "plugins/IPlugin.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#endif

namespace utsyn {

namespace {

void* openLibrary(const std::filesystem::path& p) {
#if defined(_WIN32)
    return static_cast<void*>(::LoadLibraryW(p.wstring().c_str()));
#else
    return ::dlopen(p.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

// Resolve a symbol straight to its function-pointer type. On Windows this keeps
// the cast function-pointer -> function-pointer (GetProcAddress returns FARPROC),
// avoiding MSVC C4055 (void* -> function pointer), which /WX would treat as fatal.
template <typename Fn>
Fn resolveSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<Fn>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return reinterpret_cast<Fn>(::dlsym(handle, name));
#endif
}

void closeLibrary(void* handle) {
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

const char* pluginExtension() noexcept {
#if defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

} // namespace

std::filesystem::path PluginLoader::executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::wstring(buf, len)).parent_path();
#else
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return std::filesystem::current_path();
    }
    return self.parent_path();
#endif
}

PluginLoader::~PluginLoader() {
    shutdownAll();
    unloadAll();
}

std::size_t PluginLoader::loadDirectory(const std::filesystem::path& dir, PluginContext& ctx) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        Logger::instance().warn("PluginLoader: plugin directory not found: " + dir.string());
        return 0;
    }
    std::size_t n = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        if (path.extension() != pluginExtension()) {
            continue;
        }
        // Only files named plugin_*.{dll,so} (skips utsyn_core, threepp, etc.).
        if (path.stem().string().rfind("plugin_", 0) != 0) {
            continue;
        }
        if (loadOne(path, ctx) != nullptr) {
            ++n;
        }
    }
    return n;
}

IPlugin* PluginLoader::loadOne(const std::filesystem::path& file, PluginContext& ctx) {
    void* handle = openLibrary(file);
    if (!handle) {
        Logger::instance().error("PluginLoader: failed to open " + file.string());
        return nullptr;
    }

    using CreateFn  = IPlugin* (*)();
    using DestroyFn = void (*)(IPlugin*);
    using AbiFn     = int (*)();

    auto abiFn     = resolveSymbol<AbiFn>(handle, "utsynPluginAbiVersion");
    auto createFn  = resolveSymbol<CreateFn>(handle, "createPlugin");
    auto destroyFn = resolveSymbol<DestroyFn>(handle, "destroyPlugin");

    if (!abiFn || !createFn || !destroyFn) {
        Logger::instance().error("PluginLoader: missing entry points in " + file.string());
        closeLibrary(handle);
        return nullptr;
    }
    if (abiFn() != UTSYN_PLUGIN_ABI_VERSION) {
        Logger::instance().error("PluginLoader: ABI version mismatch, skipping " + file.string());
        closeLibrary(handle);
        return nullptr;
    }

    IPlugin* plugin = createFn();
    if (!plugin) {
        Logger::instance().error("PluginLoader: createPlugin() returned null in " + file.string());
        closeLibrary(handle);
        return nullptr;
    }

    plugin->initialize(ctx);
    Logger::instance().info("PluginLoader: loaded '" + plugin->name() + "' v" + plugin->version());
    loaded_.push_back(Loaded{file.string(), handle, plugin, destroyFn, false});
    return plugin;
}

void PluginLoader::onSceneUpdate(float deltaTime) {
    for (Loaded& l : loaded_) {
        if (l.plugin) {
            l.plugin->onSceneUpdate(deltaTime);
        }
    }
}

void PluginLoader::onImGui() {
    for (Loaded& l : loaded_) {
        if (l.plugin) {
            l.plugin->onImGui();
        }
    }
}

void PluginLoader::shutdownAll() noexcept {
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
        if (it->plugin && !it->shutDown) {
            it->plugin->shutdown();
            it->shutDown = true;
        }
    }
}

void PluginLoader::unloadAll() noexcept {
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
        if (it->plugin && it->destroy) {
            it->destroy(it->plugin);
            it->plugin = nullptr;
        }
        if (it->handle) {
            closeLibrary(it->handle);
            it->handle = nullptr;
        }
    }
    loaded_.clear();
}

} // namespace utsyn
