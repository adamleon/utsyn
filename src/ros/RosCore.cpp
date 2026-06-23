#include "ros/RosCore.hpp"

#include "app/Logger.hpp"

namespace utsyn {

#if defined(UTSYN_ROS2) && UTSYN_ROS2

RosCore::RosCore() {
    if (!rclcpp::ok()) {
        rclcpp::init(0, nullptr);
        ownsRclInit_ = true;
    }
    node_     = std::make_shared<rclcpp::Node>("utsyn");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    spinThread_ = std::thread([this] { executor_->spin(); });
    Logger::instance().info("RosCore: node 'utsyn' up, executor spinning");
}

RosCore::~RosCore() { stop(); }

void RosCore::stop() noexcept {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    if (executor_) {
        executor_->cancel();
    }
    if (spinThread_.joinable()) {
        spinThread_.join();
    }
    if (executor_ && node_) {
        executor_->remove_node(node_);
    }
    node_.reset();
    executor_.reset();
    if (ownsRclInit_ && rclcpp::ok()) {
        rclcpp::shutdown();
        ownsRclInit_ = false;
    }
    Logger::instance().info("RosCore: stopped");
}

bool RosCore::rosEnabled() const noexcept { return true; }

#else // ---- built without ROS2 ----

RosCore::RosCore() {
    Logger::instance().warn(
            "RosCore: built without ROS2 (UTSYN_ROS2 off) — topics are inert");
}

RosCore::~RosCore() { stop(); }

void RosCore::stop() noexcept { stopped_ = true; }

bool RosCore::rosEnabled() const noexcept { return false; }

#endif

} // namespace utsyn
