#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ros/SubscriptionBroker.hpp"

namespace utsyn {

// Reusable per-topic streaming-feedback display. A plugin embeds one inside its
// own ImGui window and calls draw() each frame. Each watched topic renders as one
// self-contained collapsible block whose always-visible header carries the live
// signal (ASCII status token + topic + rate + age), dual-encoded by token AND
// color so it survives both collapse and colorblindness. A latched topic
// (transient_local) that has gone quiet reads healthy, not stale.
//
// Rows are created untyped via addRow() (so they render even without ROS2), then
// optionally bound to a real subscription via bind<Msg>()/bindMonitor<Msg>() —
// the only templated surface, kept header-only so Msg instantiates in the plugin
// translation unit, never in utsyn_core.
class MessageMonitor {
public:
    using RowId = std::size_t;

    explicit MessageMonitor(SubscriptionBroker& broker) noexcept : broker_(&broker) {}
    ~MessageMonitor();

    MessageMonitor(const MessageMonitor&) = delete;
    MessageMonitor& operator=(const MessageMonitor&) = delete;

    // Add a display row. Unbound rows render as ROS-disabled until a bind() call
    // attaches a subscription. `userRemovable` shows an [x] to remove the row.
    RowId addRow(std::string label, std::string topic, bool userRemovable = false);

    // Bind a row to a consuming subscription: each message is delivered to `cb` on
    // the render thread, and liveness stats are tracked. Header-only template. No-op
    // (row stays ROS-disabled) in a build without ROS2.
    template <class Msg>
    void bind(RowId row, std::function<void(const Msg&)> cb, QoSProfile qos = {}) {
        if (row >= rows_.size()) {
            return;
        }
#if defined(UTSYN_ROS2) && UTSYN_ROS2
        // The resubscribe closure is type-erased (no Msg in its signature) so it can
        // live in utsyn_core's Row; the Msg-specific subscribe<Msg> is compiled here
        // in the plugin TU. It releases the old handle and subscribes to a new topic,
        // which is also how setTopic() retargets at runtime.
        Row& r = rows_[row];
        SubscriptionBroker* broker = broker_;
        r.resubscribe = [broker, cb, qos](TopicHandle old,
                                          const std::string& topic) -> TopicHandle {
            if (old != kInvalidTopic) {
                broker->release(old);
            }
            return broker->subscribe<Msg>(topic, cb, qos);
        };
        r.handle = r.resubscribe(r.handle, r.topic);
#else
        (void)cb;
        (void)qos;
#endif
    }

    // Bind a row to a monitor-only subscription (stats, no payload delivery).
    template <class Msg>
    void bindMonitor(RowId row, QoSProfile qos = {}) {
        if (row >= rows_.size()) {
            return;
        }
#if defined(UTSYN_ROS2) && UTSYN_ROS2
        Row& r = rows_[row];
        SubscriptionBroker* broker = broker_;
        r.resubscribe = [broker, qos](TopicHandle old,
                                      const std::string& topic) -> TopicHandle {
            if (old != kInvalidTopic) {
                broker->release(old);
            }
            return broker->monitor<Msg>(topic, qos);
        };
        r.handle = r.resubscribe(r.handle, r.topic);
#else
        (void)qos;
#endif
    }

    // Retarget a bound row to a new topic (re-subscribes). No-op if the row was
    // never bound. Used by the per-row topic input box.
    void setTopic(RowId row, std::string topic);

    void remove(RowId row);

    // Render all rows + a panel-level roll-up. Call inside the plugin's Begin/End.
    void draw();

    [[nodiscard]] bool   isLive(RowId row) const;
    [[nodiscard]] double rateHz(RowId row) const;

private:
    static constexpr std::size_t kRateHistory = 96;

    struct Row {
        std::string label;
        std::string topic;
        std::array<char, 256> topicEdit{};   // editable buffer for the input box
        bool        userRemovable = false;
        bool        removed = false;
        TopicHandle handle = kInvalidTopic;
        std::function<TopicHandle(TopicHandle, const std::string&)> resubscribe;
        std::array<float, kRateHistory> rateHistory{};
        std::size_t rateHead = 0;
        bool        open = false;   // collapse state (we draw an ASCII marker, not ImGui's arrow)
    };

    void drawRow(Row& row, RowId id);

    SubscriptionBroker* broker_;
    std::vector<Row>    rows_;
};

} // namespace utsyn
