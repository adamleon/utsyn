#include "ros/SubscriptionRegistry.hpp"

#include "app/Logger.hpp"
#include "ros/RosCore.hpp"

#include <cassert>

namespace utsyn {

namespace {

TopicHandle makeHandle(std::size_t index, std::uint32_t generation) noexcept {
    // Slot index is stored +1 so that handle 0 is always invalid.
    return (static_cast<TopicHandle>(generation) << 32) |
           static_cast<TopicHandle>(index + 1);
}
std::size_t handleIndex(TopicHandle h) noexcept {
    return static_cast<std::size_t>((h & 0xFFFFFFFFu) - 1);
}
std::uint32_t handleGeneration(TopicHandle h) noexcept {
    return static_cast<std::uint32_t>(h >> 32);
}
std::string dedupKey(std::string_view topic, std::string_view type) {
    std::string k;
    k.reserve(topic.size() + type.size() + 1);
    k.append(topic);
    k.push_back('\n');
    k.append(type);
    return k;
}

} // namespace

SubscriptionRegistry::SubscriptionRegistry(RosCore& ros) noexcept
    : ros_(ros),
      start_(std::chrono::steady_clock::now()),
      renderThread_(std::this_thread::get_id()) {}

SubscriptionRegistry::~SubscriptionRegistry() { clear(); }

void SubscriptionRegistry::assertRenderThread() const noexcept {
    // acquire/release/attachDrainer/pump/clear mutate the topic map and the plain
    // int refcounts; they are only valid on the render (UI) thread. The ROS thread
    // must touch nothing here but a live Topic's StatsCell (lock-free).
    assert(std::this_thread::get_id() == renderThread_ &&
           "SubscriptionRegistry mutated off the render thread");
}

SubscriptionRegistry::Topic* SubscriptionRegistry::resolve(TopicHandle handle) noexcept {
    if (handle == kInvalidTopic) {
        return nullptr;
    }
    const std::size_t i = handleIndex(handle);
    if (i >= topics_.size()) {
        return nullptr;
    }
    Topic& t = topics_[i];
    if (t.dead || t.generation != handleGeneration(handle)) {
        return nullptr;
    }
    return &t;
}

const SubscriptionRegistry::Topic*
SubscriptionRegistry::resolve(TopicHandle handle) const noexcept {
    if (handle == kInvalidTopic) {
        return nullptr;
    }
    const std::size_t i = handleIndex(handle);
    if (i >= topics_.size()) {
        return nullptr;
    }
    const Topic& t = topics_[i];
    if (t.dead || t.generation != handleGeneration(handle)) {
        return nullptr;
    }
    return &t;
}

TopicHandle SubscriptionRegistry::acquire(std::string_view topic, std::string_view typeName,
                                          const QoSProfile& qos,
                                          const SubscriptionFactory& factory) {
    assertRenderThread();
    const std::string key = dedupKey(topic, typeName);
    if (auto it = byKey_.find(key); it != byKey_.end()) {
        Topic& existing = topics_[it->second];
        existing.refCount += 1;
        return makeHandle(it->second, existing.generation);
    }

    // Reuse a dead slot if one is parked (keeps the deque from growing without
    // bound under subscribe/release churn), else append. Either way the slot's
    // address is stable, so the ROS-thread receive closure may hold a Topic*.
    std::size_t index;
    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
        const std::uint32_t gen = topics_[index].generation; // bumped at teardown
        topics_[index] = Topic{};
        topics_[index].generation = gen;
    } else {
        index = topics_.size();
        topics_.emplace_back();
    }

    Topic& t   = topics_[index];
    t.topic    = std::string(topic);
    t.type     = std::string(typeName);
    t.qos      = qos;
    t.latched  = qos.transientLocal;
    t.refCount = 1;
    t.dead     = false;
    t.stats    = std::make_unique<StatsCell>();
    byKey_.emplace(key, index);

#if defined(UTSYN_ROS2) && UTSYN_ROS2
    {
        TopicStats initial;
        initial.state   = StreamState::NotAdvertised;
        initial.latched = t.latched;
        t.stats->publish(initial);
    }
    Topic* tp = &t;
    ReceiveTick tick = [this, tp](bool sampled, std::uint32_t bytes) {
        this->onReceive(tp, sampled, bytes);
    };
    t.subscription = factory(nodeOpaque(), std::move(tick));
    if (!t.subscription) {
        Logger::instance().error(
                "SubscriptionRegistry: subscription creation failed for " + t.topic);
    }
#else
    (void)factory;
    {
        TopicStats initial;
        initial.state   = StreamState::RosDisabled;
        initial.latched = t.latched;
        t.stats->publish(initial);
    }
#endif

    return makeHandle(index, t.generation);
}

void SubscriptionRegistry::attachDrainer(TopicHandle handle, Drainer drainer) {
    assertRenderThread();
    if (Topic* t = resolve(handle)) {
        t->drainers.push_back(std::move(drainer));
    }
}

void SubscriptionRegistry::release(TopicHandle handle) {
    assertRenderThread();
    Topic* t = resolve(handle);
    if (!t) {
        return;
    }
    if (--t->refCount > 0) {
        return;
    }

    // A drainer may release its own topic from inside pump(). Tearing it down now
    // would destroy the std::function currently executing (and free the SpscRing /
    // callback it captured) -> use-after-free. Defer teardown until pump() unwinds.
    // The topic stays alive (and re-acquirable) during the rest of the frame.
    if (pumping_) {
        pendingRelease_.push_back(handleIndex(handle));
        return;
    }
    teardownTopic(*t, handleIndex(handle));
}

void SubscriptionRegistry::teardownTopic(Topic& t, std::size_t index) {
    // Drop the closures (which capture plugin-defined message types) and the rclcpp
    // subscription. Bump generation so outstanding handles stop resolving, and park
    // the slot on the free-list for reuse. The StatsCell is left intact (cheap, and
    // a dead topic never resolves) until the slot is reused or clear() runs.
    // TODO(ROS2-on): release() can run on the render thread while the executor is
    // still spinning; reset() of a live subscription must be synchronized with the
    // ROS thread (or release() restricted to a stopped executor). See review M1.
    byKey_.erase(dedupKey(t.topic, t.type));
    t.drainers.clear();
    t.subscription.reset();
    t.dead = true;
    t.generation += 1;
    freeList_.push_back(index);
}

TopicStats SubscriptionRegistry::stats(TopicHandle handle) const noexcept {
    if (const Topic* t = resolve(handle)) {
        if (t->stats) {
            return t->stats->snapshot();
        }
    }
    return TopicStats{}; // RosDisabled default — a no-ROS / invalid-handle row
}

std::string SubscriptionRegistry::topicName(TopicHandle handle) const {
    const Topic* t = resolve(handle);
    return t ? t->topic : std::string{};
}

std::string SubscriptionRegistry::typeName(TopicHandle handle) const {
    const Topic* t = resolve(handle);
    return t ? t->type : std::string{};
}

void SubscriptionRegistry::pump() {
    assertRenderThread();
    // Iterate by index with a size snapshot: a drainer may subscribe (acquire ->
    // deque growth) mid-loop, which would invalidate a range-for iterator. New
    // topics added this frame are intentionally serviced from the next frame.
    pumping_ = true;
    for (std::size_t i = 0, n = topics_.size(); i < n; ++i) {
        Topic& t = topics_[i];
        if (t.dead) {
            continue;
        }
        for (std::size_t j = 0; j < t.drainers.size(); ++j) {
            t.drainers[j]();
        }
    }
    pumping_ = false;

    // Run releases that were deferred while pumping. Skip any topic that was
    // re-acquired during the frame (refCount back above 0) or already torn down.
    for (const std::size_t idx : pendingRelease_) {
        if (idx < topics_.size() && !topics_[idx].dead && topics_[idx].refCount == 0) {
            teardownTopic(topics_[idx], idx);
        }
    }
    pendingRelease_.clear();
}

void SubscriptionRegistry::clear() {
    assertRenderThread();
    // The ROS thread must be stopped first, else a subscription callback could fire
    // into a half-torn-down registry. Application::shutdown() guarantees this.
    assert(ros_.stopped() && "registry cleared while the ROS thread is still spinning");
    for (Topic& t : topics_) {
        t.drainers.clear();
        t.subscription.reset();
        t.stats.reset();
    }
    topics_.clear();
    byKey_.clear();
    freeList_.clear();
    pendingRelease_.clear();
}

#if defined(UTSYN_ROS2) && UTSYN_ROS2

void* SubscriptionRegistry::nodeOpaque() noexcept {
    auto node = ros_.node();
    return node ? static_cast<void*>(node.get()) : nullptr;
}

double SubscriptionRegistry::steadySeconds() const noexcept {
    // Absolute steady_clock seconds (time since the clock's epoch), NOT relative to
    // start_. The render-thread widget compares this against its own
    // steady_clock::now() to age messages, so both must share the same epoch.
    return std::chrono::duration<double>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

void SubscriptionRegistry::onReceive(Topic* t, bool sampled, std::uint32_t bytes) noexcept {
    const double now = steadySeconds();
    if (t->gotFirst) {
        const double dt = now - t->lastReceiptMono;
        if (dt > 1e-9) {
            const double inst = 1.0 / dt;
            // EWMA so the displayed rate is steady rather than per-message jittery.
            t->rateHz = t->gotRate ? (0.2 * inst + 0.8 * t->rateHz) : inst;
            t->gotRate = true;
        }
    }
    t->lastReceiptMono = now;
    t->count += 1;
    t->gotFirst = true;
    if (sampled) {
        t->lastBytes = bytes;
    }
    publishStats(*t);
}

void SubscriptionRegistry::publishStats(Topic& t) noexcept {
    TopicStats s;
    // ROS-thread truth is "Live once a message arrived"; the widget overlays the
    // time-based stale judgment and the latched-is-healthy rule.
    s.state            = StreamState::Live;
    s.latched          = t.latched;
    s.publisherPresent = true;
    s.messageCount     = t.count;
    s.rateHz           = t.rateHz;
    s.lastReceiptMono  = t.lastReceiptMono;
    s.lastMsgBytes     = t.lastBytes;
    t.stats->publish(s);
}

#endif // UTSYN_ROS2

} // namespace utsyn
