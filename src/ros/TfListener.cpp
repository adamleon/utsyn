#include "TfListener.hpp"

#include <set>

#if defined(UTSYN_ROS2) && UTSYN_ROS2
#  include "ros/SubscriptionBroker.hpp"

#  include <tf2_msgs/msg/tf_message.hpp>
#endif

namespace utsyn {

#if defined(UTSYN_ROS2) && UTSYN_ROS2
namespace {

// Copy every TransformStamped in a TFMessage into the graph (runs on the render thread
// via the broker pump). header.frame_id is the parent, child_frame_id the child.
void ingestInto(TfListener& tf, const tf2_msgs::msg::TFMessage& m, bool isStatic) {
    for (const auto& ts : m.transforms) {
        TfTransform t;
        t.tx = ts.transform.translation.x;
        t.ty = ts.transform.translation.y;
        t.tz = ts.transform.translation.z;
        t.qx = ts.transform.rotation.x;
        t.qy = ts.transform.rotation.y;
        t.qz = ts.transform.rotation.z;
        t.qw = ts.transform.rotation.w;
        const double stamp = static_cast<double>(ts.header.stamp.sec) +
                             static_cast<double>(ts.header.stamp.nanosec) * 1e-9;
        tf.setTransform(ts.child_frame_id, ts.header.frame_id, t, isStatic, stamp);
    }
}

} // namespace
#endif

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

#if defined(UTSYN_ROS2) && UTSYN_ROS2
void TfListener::subscribe(SubscriptionBroker& broker) {
    broker.subscribe<tf2_msgs::msg::TFMessage>(
            "/tf", [this](const tf2_msgs::msg::TFMessage& m) { ingestInto(*this, m, false); });
    broker.subscribe<tf2_msgs::msg::TFMessage>(
            "/tf_static",
            [this](const tf2_msgs::msg::TFMessage& m) { ingestInto(*this, m, true); },
            QoSProfile::latched());
}
#endif

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
