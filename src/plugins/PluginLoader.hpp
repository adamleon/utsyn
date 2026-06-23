#pragma once

namespace utsyn {

class IPlugin;

// Dynamic .so/.dll loading. Resolves createPlugin/destroyPlugin symbols.
class PluginLoader {
public:
    // TODO: load(path), unload(plugin)

private:
};

} // namespace utsyn
