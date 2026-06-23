#pragma once

#include <atomic>
#include <cstdint>

namespace utsyn {

// Liveness of a monitored topic as seen by the ROS thread. The render-side widget
// overlays a time-derived "stale" judgment (now - lastReceiptMono vs a threshold)
// on top of Live, because a stream that simply stops sending produces no callback
// to flip the state. `latched` lets the widget show a quiet transient_local topic
// (e.g. /robot_description) as healthy rather than stale.
enum class StreamState : std::uint8_t {
    RosDisabled = 0,   // built without ROS2 (UTSYN_ROS2 off)
    NotAdvertised,     // no publisher seen yet / no message received yet
    AdvertisedNoData,  // publisher present on the graph, no message yet
    Live,              // a message has been received
    LatchedReceived,   // transient_local retained value received; healthy, not stale
    TypeMismatch,      // publisher type != subscribed type
    QosIncompatible,   // requested QoS incompatible with publisher
};

// Trivially copyable so it can ride a double-buffer published with a single atomic
// index. Keep it POD: no std types, no pointers that imply ownership.
struct TopicStats {
    StreamState   state            = StreamState::RosDisabled;
    bool          latched          = false;  // subscription is transient_local
    bool          publisherPresent = false;
    std::uint64_t messageCount     = 0;
    double        rateHz           = 0.0;    // EWMA of arrival rate, ROS-thread computed
    double        lastReceiptMono  = 0.0;    // absolute steady_clock seconds; 0 == never
    std::uint32_t lastMsgBytes     = 0;      // sampled (see SubscriptionBroker); 0 == unsampled
    char          resolvedType[128] = {};    // actual publisher type, for mismatch display
    std::uint32_t reserved[4]       = {};    // headroom for future fields without ABI churn
};

// Single-writer (ROS thread) / single-reader (render thread) lock-free hand-off.
//
// Published-index double-buffer: the writer fills the inactive slot, then
// publishes its index with a release store; the reader acquire-loads the index
// and copies that slot. No torn reads, no locks, no retry loop. This is the
// sanctioned ROS->render primitive for the per-topic stats channel (the payload
// channel uses SpscRing).
class StatsCell {
public:
    void publish(const TopicStats& s) noexcept {            // ROS thread only
        const unsigned next = active_.load(std::memory_order_relaxed) ^ 1u;
        buf_[next] = s;
        active_.store(next, std::memory_order_release);
    }

    [[nodiscard]] TopicStats snapshot() const noexcept {    // render thread only
        const unsigned i = active_.load(std::memory_order_acquire);
        return buf_[i];
    }

private:
    std::atomic<unsigned> active_{0};
    TopicStats            buf_[2];
};

} // namespace utsyn
