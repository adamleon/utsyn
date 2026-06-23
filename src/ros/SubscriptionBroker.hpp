#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "ros/ISubscriptionRegistry.hpp"

#if defined(UTSYN_ROS2) && UTSYN_ROS2
#  include <memory>
#  include <rclcpp/rclcpp.hpp>
#  include "ros/SpscRing.hpp"
#endif

namespace utsyn {

#if defined(UTSYN_ROS2) && UTSYN_ROS2
// QoSProfile -> rclcpp::QoS. Header-only so it instantiates in the plugin TU.
inline rclcpp::QoS toRclQos(const QoSProfile& q) {
    rclcpp::QoS qos{rclcpp::KeepLast(q.depth ? q.depth : 1)};
    if (q.transientLocal) {
        qos.transient_local();
    }
    if (q.bestEffort) {
        qos.best_effort();
    }
    return qos;
}
#endif

// Typed facade over ISubscriptionRegistry. This is the ONLY templated surface in
// the subscription system, and it is fully header-defined, so every
// message-type-specific instruction (create_subscription<Msg>, the typed lambda,
// the type-name trait, SpscRing<Msg>) compiles into the *plugin* DLL. utsyn_core
// exports only the non-template ISubscriptionRegistry, so no message template ever
// crosses the DLL boundary. Plugins reach this through PluginContext::broker.
class SubscriptionBroker {
public:
    explicit SubscriptionBroker(ISubscriptionRegistry& registry) noexcept
        : registry_(registry) {}

    [[nodiscard]] ISubscriptionRegistry& registry() noexcept { return registry_; }

    // Subscribe and deliver each message to `cb` on the RENDER thread (via the
    // pump() drainer), while tracking liveness stats. Returns kInvalidTopic in a
    // build without ROS2 — the monitor row then renders as ROS-disabled.
    template <typename Msg>
    TopicHandle subscribe(std::string_view topic,
                          std::function<void(const Msg&)> cb,
                          QoSProfile qos = {}) {
#if defined(UTSYN_ROS2) && UTSYN_ROS2
        const std::string typeName = rosidl_generator_traits::name<Msg>();
        auto queue = std::make_shared<SpscRing<Msg>>(256);

        SubscriptionFactory factory =
                [topicStr = std::string(topic), qos, queue](
                        void* nodeOpaque, ReceiveTick onReceive) -> std::shared_ptr<void> {
            auto* node = static_cast<rclcpp::Node*>(nodeOpaque);
            return node->create_subscription<Msg>(
                    topicStr, toRclQos(qos),
                    [queue, onReceive = std::move(onReceive)](
                            std::shared_ptr<const Msg> msg) { // ROS thread
                        onReceive(false, 0u);                 // count / rate / last-receipt
                        queue->push(std::move(msg));
                    });
        };

        const TopicHandle handle = registry_.acquire(topic, typeName, qos, factory);
        registry_.attachDrainer(handle, [queue, cb = std::move(cb)] { // render thread
            std::shared_ptr<const Msg> msg;
            while (queue->pop(msg)) {
                if (msg) {
                    cb(*msg);
                }
            }
        });
        return handle;
#else
        (void)topic;
        (void)cb;
        (void)qos;
        return kInvalidTopic;
#endif
    }

    // Monitor-only: track liveness stats without delivering payloads to a callback.
    template <typename Msg>
    TopicHandle monitor(std::string_view topic, QoSProfile qos = {}) {
#if defined(UTSYN_ROS2) && UTSYN_ROS2
        const std::string typeName = rosidl_generator_traits::name<Msg>();
        SubscriptionFactory factory =
                [topicStr = std::string(topic), qos](
                        void* nodeOpaque, ReceiveTick onReceive) -> std::shared_ptr<void> {
            auto* node = static_cast<rclcpp::Node*>(nodeOpaque);
            return node->create_subscription<Msg>(
                    topicStr, toRclQos(qos),
                    [onReceive = std::move(onReceive)](std::shared_ptr<const Msg>) {
                        onReceive(false, 0u);
                    });
        };
        return registry_.acquire(topic, typeName, qos, factory);
#else
        (void)topic;
        (void)qos;
        return kInvalidTopic;
#endif
    }

    [[nodiscard]] TopicStats stats(TopicHandle handle) const noexcept {
        return registry_.stats(handle);
    }
    [[nodiscard]] std::string topicName(TopicHandle handle) const {
        return registry_.topicName(handle);
    }
    void release(TopicHandle handle) { registry_.release(handle); }
    void pump() { registry_.pump(); }

private:
    ISubscriptionRegistry& registry_;
};

} // namespace utsyn
