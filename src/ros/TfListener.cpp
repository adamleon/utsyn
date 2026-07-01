#include "TfListener.hpp"

#include <set>

namespace utsyn {

void TfListener::setTransform(const std::string& child, const std::string& parent,
                              const TfTransform& t, bool isStatic, double stamp) {
    const std::lock_guard<std::mutex> lk(mutex_);
    frames_[child] = TfFrame{parent, t, isStatic, stamp};
}

void TfListener::loadDemoTree() {
    // A small robot-like hierarchy so the panel shows a tree without a ROS graph.
    // Translations only (identity rotation); enough to exercise the tree rendering.
    const TfTransform link{0.0, 0.0, 0.3, 0.0, 0.0, 0.0, 1.0};
    setTransform("base_link", "world", TfTransform{}, true, 0.0);
    setTransform("shoulder_link", "base_link", link, false, 0.0);
    setTransform("upper_arm_link", "shoulder_link", link, false, 0.0);
    setTransform("forearm_link", "upper_arm_link", link, false, 0.0);
    setTransform("wrist_link", "forearm_link", link, false, 0.0);
    setTransform("tool0", "wrist_link", TfTransform{0.0, 0.0, 0.1, 0.0, 0.0, 0.0, 1.0}, false, 0.0);
    // A second branch off base_link so the tree shows multiple children.
    setTransform("camera_link", "base_link", TfTransform{0.1, 0.0, 0.2, 0.0, 0.0, 0.0, 1.0}, true,
                 0.0);
}

TfListener::Snapshot TfListener::snapshot() const {
    const std::lock_guard<std::mutex> lk(mutex_);

    Snapshot s;
    s.frames = frames_; // copy

    std::set<std::string> allChildren;
    std::set<std::string> allNames;
    for (const auto& [child, f] : frames_) {
        allChildren.insert(child);
        allNames.insert(child);
        if (!f.parent.empty()) {
            allNames.insert(f.parent);
            s.children[f.parent].push_back(child);
        }
    }

    // A root is a frame that never appears as a child (the top of a chain, e.g. "world"
    // — which may have no transform entry of its own), or a frame with an empty parent.
    for (const auto& name : allNames) {
        const auto it = frames_.find(name);
        const bool isChild = it != frames_.end();
        if (!isChild || it->second.parent.empty()) {
            s.roots.push_back(name);
        }
    }
    return s;
}

std::size_t TfListener::frameCount() const {
    const std::lock_guard<std::mutex> lk(mutex_);
    return frames_.size();
}

} // namespace utsyn
