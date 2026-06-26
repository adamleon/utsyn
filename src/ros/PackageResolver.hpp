#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace utsyn {

// Resolves ROS-style "package://<pkg>/<rel>" URIs to absolute filesystem paths
// using an ordered list of search roots. A *root* is a directory that directly
// contains package directories, so "<root>/<pkg>/<rel>" is the candidate file —
// the same shape as a ROS install's `share/` directory or a `ROS_PACKAGE_PATH`
// entry. The first root that contains the file wins.
//
// Why this exists: threepp's URDFLoader cannot resolve `package://` unless the
// mesh sits next to an on-disk `package.xml` reachable from the URDF's baseDir.
// When a URDF arrives over the wire (latched `/robot_description`) from a *different*
// ROS install than the one utsyn runs in, utsyn has no baseDir and no package.xml
// to walk up to — so meshes silently drop. This resolver rewrites the URI to the
// absolute on-disk path before parsing, decoupling "where the robot's packages
// live on this machine" from "which ROS env utsyn itself runs in".
//
// Roots are gathered, highest priority first, from:
//   1. roots added via addRoot()        (explicit, code-side)
//   2. UTSYN_PACKAGE_PATH               utsyn-specific override; entries are roots
//   3. AMENT_PREFIX_PATH                ROS install prefixes; "<prefix>/share" is the root
//   4. ROS_PACKAGE_PATH                 ROS1-style; entries are roots
// List entries are separated by ';' on Windows and ':' elsewhere.
//
// rclcpp-free: pure environment + std::filesystem. Safe to construct and use with
// ROS2 compiled out.
class PackageResolver {
public:
    // Builds the search roots from the environment (see class doc).
    PackageResolver();

    // Add an explicit search root at the FRONT (outranks every env-derived root).
    PackageResolver& addRoot(std::filesystem::path root);

    // Resolve one "package://pkg/rel" URI to an existing absolute path, or an empty
    // path if no root contains it (or the input is not a package:// URI).
    [[nodiscard]] std::filesystem::path resolve(std::string_view packageUri) const;

    // Rewrite every "package://..." URI inside a URDF/XML string to the absolute
    // path it resolves to (forward-slashed). Unresolved URIs are left untouched, so
    // the loader skips just those meshes. This is a pure string transform — it does
    // NOT parse XML; it relies on package:// only ever appearing inside mesh
    // filename attributes. Optionally reports how many URIs were (un)resolved.
    [[nodiscard]] std::string rewriteUrdf(const std::string& urdf,
                                          int* resolvedCount   = nullptr,
                                          int* unresolvedCount = nullptr) const;

    [[nodiscard]] const std::vector<std::filesystem::path>& roots() const noexcept {
        return roots_;
    }

private:
    // Split an env var by the platform list separator and append each entry as a
    // root; when asShare is true, append "share" to each entry first (the ament
    // prefix → share-dir convention).
    void appendListEnv(const char* name, bool asShare);

    std::vector<std::filesystem::path> roots_;
};

} // namespace utsyn
