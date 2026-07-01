#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace utsyn {

class SubscriptionBroker;

// A single parent -> child transform in the TF graph.
struct TfTransform {
    double tx = 0.0, ty = 0.0, tz = 0.0;           // translation
    double qx = 0.0, qy = 0.0, qz = 0.0, qw = 1.0; // rotation (quaternion)
};

struct TfFrame {
    std::string parent;          // parent frame name; empty for a root
    TfTransform transform;       // parent -> this frame
    bool        isStatic = false;
    double      stamp    = 0.0;  // seconds; when this transform was last set
};

// Maintains the TF frame graph. The ROS thread writes transforms via setTransform (from
// /tf + /tf_static callbacks, wired later); the render thread reads a snapshot() for the
// TfTree widget. Thread-safe via a mutex — TF is low-rate and the critical sections are
// tiny, so this doesn't need the lock-free double-buffer the topic stats use.
class TfListener {
public:
    // Immutable view the render thread renders from — includes derived roots/children so
    // the widget doesn't recompute the tree shape each frame.
    struct Snapshot {
        std::map<std::string, TfFrame>                  frames;   // child name -> frame
        std::vector<std::string>                        roots;    // never appear as a child
        std::map<std::string, std::vector<std::string>> children; // parent -> child names
    };

    // Set the parent -> child transform (ROS thread or demo). Thread-safe.
    void setTransform(const std::string& child, const std::string& parent, const TfTransform& t,
                      bool isStatic, double stamp);

    // Populate a synthetic robot-like tree so the widget is usable offline (no ROS graph).
    void loadDemoTree();

#if defined(UTSYN_ROS2) && UTSYN_ROS2
    // Subscribe to /tf (dynamic) + /tf_static (latched) via the broker. Callbacks deliver
    // on the render thread (broker pump) and feed setTransform.
    void subscribe(SubscriptionBroker& broker);
#endif

    [[nodiscard]] Snapshot    snapshot() const; // thread-safe copy + derived roots/children
    [[nodiscard]] std::size_t frameCount() const;

private:
    mutable std::mutex             mutex_;
    std::map<std::string, TfFrame> frames_; // child name -> frame
};

} // namespace utsyn
