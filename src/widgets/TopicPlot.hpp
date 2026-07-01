#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace utsyn {

// Real-time multi-channel line plot (ImPlot). Each named channel is a time-windowed ring
// of (t, value) samples; push() adds a sample stamped with the current ImGui time; onImGui
// draws every channel on an x-axis that scrolls to show the last historySeconds.
//
// Data source is external: a /joint_states subscription pushes joint positions live, and
// offline a synthetic signal is pushed so the panel is usable without a ROS graph.
class TopicPlot {
public:
    // Add a sample to `channel` (created on first use), stamped with ImGui::GetTime().
    // Call on the render thread (it reads the ImGui clock).
    void push(const std::string& channel, float value);

    void onImGui(bool* pOpen = nullptr);

    [[nodiscard]] std::size_t channelCount() const { return channels_.size(); }

private:
    struct Channel {
        std::string        name;
        std::vector<float> t; // ring buffer of timestamps (seconds)
        std::vector<float> v; // ring buffer of values (parallel to t)
        int                offset = 0; // next write index once full
    };
    Channel& channelFor(const std::string& name);

    std::vector<Channel> channels_;
    float                historySeconds_ = 10.0f;
};

} // namespace utsyn
