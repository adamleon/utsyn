#pragma once

#if defined(UTSYN_ROS2) && UTSYN_ROS2
#  include <memory>
#  include <thread>
#  include <rclcpp/rclcpp.hpp>
#endif

namespace utsyn {

// Owns the ROS2 lifecycle: rclcpp init/shutdown, the single "utsyn" node, a
// SingleThreadedExecutor, and the dedicated spin thread. This realizes the "ROS2
// thread" that ARCHITECTURE.md describes. In a build without ROS2 (UTSYN_ROS2
// undefined/0) it is an inert object whose rosEnabled() is false and which starts
// no thread — the rest of the system links and runs unchanged.
//
// The single-threaded executor is load-bearing: it is the contract that makes the
// per-topic StatsCell a single-writer (all subscription callbacks and the future
// graph-poll timer run on this one thread).
class RosCore {
public:
    RosCore();
    ~RosCore();

    RosCore(const RosCore&) = delete;
    RosCore& operator=(const RosCore&) = delete;

    // Stop the spin thread and shut rclcpp down. Idempotent. Call this BEFORE
    // unloading plugin DLLs so no subscription callback fires into freed code.
    void stop() noexcept;

    [[nodiscard]] bool rosEnabled() const noexcept;

    // True once stop() has run (or in a no-ROS build, where the spin thread never
    // existed). The registry asserts this before tearing subscriptions down.
    [[nodiscard]] bool stopped() const noexcept { return stopped_; }

#if defined(UTSYN_ROS2) && UTSYN_ROS2
    // The shared node. rclcpp permits creating subscriptions from another thread;
    // the executor picks them up. Used by SubscriptionRegistry.
    [[nodiscard]] rclcpp::Node::SharedPtr node() const noexcept { return node_; }
#endif

private:
#if defined(UTSYN_ROS2) && UTSYN_ROS2
    rclcpp::Node::SharedPtr                                       node_;
    std::shared_ptr<rclcpp::executors::SingleThreadedExecutor>    executor_;
    std::thread                                                   spinThread_;
    bool                                                          ownsRclInit_ = false;
#endif
    bool stopped_ = false;
};

} // namespace utsyn
