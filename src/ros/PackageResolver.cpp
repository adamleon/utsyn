#include "ros/PackageResolver.hpp"

#include <cstdlib>
#include <optional>
#include <system_error>
#include <utility>

namespace utsyn {

namespace {

#ifdef _WIN32
constexpr char kEnvListSep = ';';
#else
constexpr char kEnvListSep = ':';
#endif

// Read an environment variable. Uses _dupenv_s on Windows because std::getenv
// trips MSVC's C4996 ("may be unsafe"), which is fatal under /WX.
std::optional<std::string> getEnv(const char* name) {
#ifdef _WIN32
    char*  buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
        return std::nullopt;
    }
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* v = std::getenv(name);
    if (v == nullptr) {
        return std::nullopt;
    }
    return std::string(v);
#endif
}

} // namespace

PackageResolver::PackageResolver() {
    // Highest env priority first. addRoot() later inserts ahead of all of these.
    appendListEnv("UTSYN_PACKAGE_PATH", /*asShare=*/false);
    appendListEnv("AMENT_PREFIX_PATH", /*asShare=*/true);
    appendListEnv("ROS_PACKAGE_PATH", /*asShare=*/false);
}

void PackageResolver::appendListEnv(const char* name, bool asShare) {
    const auto value = getEnv(name);
    if (!value) {
        return;
    }
    std::size_t start = 0;
    while (start <= value->size()) {
        const std::size_t sep = value->find(kEnvListSep, start);
        const std::size_t end = (sep == std::string::npos) ? value->size() : sep;
        if (end > start) {
            std::filesystem::path p = value->substr(start, end - start);
            if (asShare) {
                p /= "share";
            }
            roots_.push_back(std::move(p));
        }
        if (sep == std::string::npos) {
            break;
        }
        start = sep + 1;
    }
}

PackageResolver& PackageResolver::addRoot(std::filesystem::path root) {
    roots_.insert(roots_.begin(), std::move(root));
    return *this;
}

std::filesystem::path PackageResolver::resolve(std::string_view packageUri) const {
    constexpr std::string_view kPrefix = "package://";
    if (!packageUri.starts_with(kPrefix)) {
        return {};
    }
    const std::string_view rel = packageUri.substr(kPrefix.size()); // "pkg/rel..."
    if (rel.empty()) {
        return {};
    }
    const std::filesystem::path relPath{std::string(rel)};
    for (const auto& root : roots_) {
        std::error_code     ec;
        std::filesystem::path candidate = root / relPath;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.lexically_normal();
        }
    }
    return {};
}

std::string PackageResolver::rewriteUrdf(const std::string& urdf, int* resolvedCount,
                                         int* unresolvedCount) const {
    constexpr std::string_view kMarker = "package://";

    int         resolved   = 0;
    int         unresolved = 0;
    std::string out;
    out.reserve(urdf.size());

    std::size_t pos = 0;
    while (true) {
        const std::size_t hit = urdf.find(kMarker, pos);
        if (hit == std::string::npos) {
            out.append(urdf, pos, std::string::npos);
            break;
        }
        out.append(urdf, pos, hit - pos); // copy everything before the URI

        // The URI runs until the first character that can't be part of a filename
        // attribute value — a quote, an angle bracket, or whitespace.
        std::size_t end = hit;
        while (end < urdf.size()) {
            const char c = urdf[end];
            if (c == '"' || c == '\'' || c == '<' || c == '>' || c == ' ' ||
                c == '\t' || c == '\r' || c == '\n') {
                break;
            }
            ++end;
        }

        const std::string_view  uri(urdf.data() + hit, end - hit);
        const std::filesystem::path abs = resolve(uri);
        if (!abs.empty()) {
            out.append(abs.generic_string());
            ++resolved;
        } else {
            out.append(uri); // leave untouched; the loader will skip this mesh
            ++unresolved;
        }
        pos = end;
    }

    if (resolvedCount != nullptr) {
        *resolvedCount = resolved;
    }
    if (unresolvedCount != nullptr) {
        *unresolvedCount = unresolved;
    }
    return out;
}

} // namespace utsyn
