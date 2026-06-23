#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ros/ISubscriptionRegistry.hpp"
#include "ros/TopicStats.hpp"

namespace utsyn {

class RosCore;

// Concrete ISubscriptionRegistry living entirely in utsyn_core. Owns per-topic
// state: the dedup map, refcounts, the StatsCell double-buffer, the type-erased
// rclcpp subscription, and the per-consumer drainers. rclcpp appears only in the
// .cpp (guarded by UTSYN_ROS2); this header is rclcpp-free.
//
// Threading: the registry map (acquire/release/attachDrainer/pump/clear) is
// touched only on the render thread. The ROS spin thread only ever calls
// onReceive(), which mutates a single Topic's own receive bookkeeping and
// publishes its StatsCell — never the map. Topic addresses are stable (std::deque,
// append-only) so the ROS-thread receive closure can hold a Topic* safely.
class SubscriptionRegistry final : public ISubscriptionRegistry {
public:
    explicit SubscriptionRegistry(RosCore& ros) noexcept;
    ~SubscriptionRegistry() override;

    SubscriptionRegistry(const SubscriptionRegistry&) = delete;
    SubscriptionRegistry& operator=(const SubscriptionRegistry&) = delete;

    TopicHandle acquire(std::string_view topic, std::string_view typeName,
                        const QoSProfile& qos,
                        const SubscriptionFactory& factory) override;
    void attachDrainer(TopicHandle handle, Drainer drainer) override;
    void release(TopicHandle handle) override;
    [[nodiscard]] TopicStats  stats(TopicHandle handle) const noexcept override;
    [[nodiscard]] std::string topicName(TopicHandle handle) const override;
    [[nodiscard]] std::string typeName(TopicHandle handle) const override;
    void pump() override;
    void clear() override;

private:
    struct Topic {
        std::string                topic;
        std::string                type;
        QoSProfile                 qos{};
        bool                       latched = false;
        int                        refCount = 0;
        std::uint32_t              generation = 0;
        bool                       dead = false;
        std::unique_ptr<StatsCell> stats;        // heap-stable (atomic, non-movable)
        std::shared_ptr<void>      subscription; // rclcpp::SubscriptionBase, type-erased
        std::vector<Drainer>       drainers;

        // ROS-thread-only receive bookkeeping:
        std::uint64_t              count = 0;
        double                     lastReceiptMono = 0.0;
        double                     rateHz = 0.0;
        std::uint32_t              lastBytes = 0;
        bool                       gotFirst = false;
        bool                       gotRate = false;
    };

    [[nodiscard]] Topic*       resolve(TopicHandle handle) noexcept;
    [[nodiscard]] const Topic* resolve(TopicHandle handle) const noexcept;
    void teardownTopic(Topic& t, std::size_t index);
    void assertRenderThread() const noexcept;

#if defined(UTSYN_ROS2) && UTSYN_ROS2
    void  onReceive(Topic* t, bool sampled, std::uint32_t bytes) noexcept; // ROS thread
    void  publishStats(Topic& t) noexcept;                                 // ROS thread
    void* nodeOpaque() noexcept;
    [[nodiscard]] double steadySeconds() const noexcept;
#endif

    [[maybe_unused]] RosCore&                            ros_;
    [[maybe_unused]] std::chrono::steady_clock::time_point start_;
    std::thread::id                                      renderThread_;  // mutations must run here
    std::deque<Topic>                                    topics_;  // stable addresses; slots reused via freeList_
    std::unordered_map<std::string, std::size_t>         byKey_;   // "topic\ntype" -> live index
    std::vector<std::size_t>                             freeList_;      // dead slots awaiting reuse
    std::vector<std::size_t>                             pendingRelease_;// releases deferred out of pump()
    bool                                                 pumping_ = false;
};

} // namespace utsyn
