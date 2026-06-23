#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "ros/TopicStats.hpp"

namespace utsyn {

// Opaque handle to a deduplicated (topic, type) subscription. 0 == invalid. The
// value encodes {slot index, generation} so a stale handle (after release) does
// not resolve to a reused slot.
using TopicHandle = std::uint64_t;
inline constexpr TopicHandle kInvalidTopic = 0;

// rclcpp-free QoS description. Mapped to rclcpp::QoS inside the broker facade /
// registry .cpp so this header (and everything that includes it) need not pull
// rclcpp — which is what keeps the no-ROS build and the widget compiling.
struct QoSProfile {
    std::uint16_t depth          = 10;
    bool          transientLocal = false;  // true for latched topics (/robot_description)
    bool          bestEffort     = false;

    static QoSProfile latched() noexcept { return QoSProfile{1, true, false}; }
};

// Called from the ROS thread on each received message. `sampled` is true when
// `bytes` carries a fresh serialized-size sample (size sampling is throttled, so
// most calls pass false/0). Supplied by the registry to the factory below.
using ReceiveTick = std::function<void(bool sampled, std::uint32_t bytes)>;

// Supplied by the plugin translation unit — the ONLY place a concrete message
// type is named. Given the node as an opaque pointer (cast back to rclcpp::Node*
// inside the plugin TU, which links the message packages), it creates the typed
// subscription and returns it type-erased as a shared_ptr<void>. No message
// template ever crosses into utsyn_core through this interface.
using SubscriptionFactory =
    std::function<std::shared_ptr<void>(void* nodeOpaque, ReceiveTick onReceive)>;

// Pops a consumer's payload queue and invokes its callback. Runs on the render
// thread inside pump(). Stored type-erased so core never names the message type.
using Drainer = std::function<void()>;

// Non-template, ABI-stable contract implemented in utsyn_core. APPEND-ONLY: never
// reorder or remove virtuals; add new ones at the END only and bump
// UTSYN_PLUGIN_ABI_VERSION when you do.
class ISubscriptionRegistry {
public:
    virtual ~ISubscriptionRegistry() = default;

    // Acquire a (topic, type) subscription. Deduplicated + refcounted: repeated
    // acquire of the same (topic, type) shares one rclcpp subscription and one
    // StatsCell, returning the same handle. `factory` runs only on first acquire.
    virtual TopicHandle acquire(std::string_view topic, std::string_view typeName,
                                const QoSProfile& qos,
                                const SubscriptionFactory& factory) = 0;

    // Register a render-thread drainer on an existing handle (one per consumer).
    virtual void attachDrainer(TopicHandle handle, Drainer drainer) = 0;

    // Decrement refcount; the last release tears the subscription + cell down.
    virtual void release(TopicHandle handle) = 0;

    // Render-thread lock-free reads. stats() on an invalid handle returns a
    // RosDisabled snapshot (so a no-ROS monitor row renders sensibly).
    [[nodiscard]] virtual TopicStats  stats(TopicHandle handle) const noexcept = 0;
    [[nodiscard]] virtual std::string topicName(TopicHandle handle) const = 0;
    [[nodiscard]] virtual std::string typeName(TopicHandle handle) const = 0;

    // Called once per frame on the render thread: runs every drainer, delivering
    // queued messages to consumer callbacks. Keeps user callbacks OFF the ROS thread.
    virtual void pump() = 0;

    // Drop every subscription + drainer. MUST be called (after the ROS thread is
    // stopped) before unloading plugin DLLs, because drainers and subscriptions
    // capture closures over plugin-defined message types.
    virtual void clear() = 0;
};

} // namespace utsyn
