#pragma once

namespace utsyn {

// Owns all rclcpp subscriptions. Plugins request topics through it; it
// deduplicates (N plugins -> 1 subscription) and pushes messages into
// per-topic queues consumed by the render thread. Lives on the ROS2 thread.
class SubscriptionBroker {
public:
    // TODO: template <typename Msg> void subscribe(topic, callback);

private:
};

} // namespace utsyn
