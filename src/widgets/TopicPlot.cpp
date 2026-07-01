#include "TopicPlot.hpp"

#include <imgui.h>
#include <implot.h>

namespace utsyn {

namespace {
constexpr int kCap = 2400; // max samples kept per channel (ring buffer)
} // namespace

TopicPlot::Channel& TopicPlot::channelFor(const std::string& name) {
    for (auto& ch : channels_) {
        if (ch.name == name) {
            return ch;
        }
    }
    channels_.push_back(Channel{name, {}, {}, 0});
    Channel& ch = channels_.back();
    ch.t.reserve(kCap);
    ch.v.reserve(kCap);
    return ch;
}

void TopicPlot::push(const std::string& channel, float value) {
    Channel&    ch = channelFor(channel);
    const float t  = static_cast<float>(ImGui::GetTime());
    if (static_cast<int>(ch.t.size()) < kCap) {
        ch.t.push_back(t);
        ch.v.push_back(value);
    } else {
        ch.t[ch.offset] = t;
        ch.v[ch.offset] = value;
        ch.offset       = (ch.offset + 1) % kCap;
    }
}

void TopicPlot::onImGui(bool* pOpen) {
    if (!ImGui::Begin("Topic Plot", pOpen)) {
        ImGui::End();
        return;
    }

    if (channels_.empty()) {
        ImGui::TextDisabled("No data.");
        ImGui::TextDisabled("(/joint_states positions plot here when ROS2 is on.)");
        ImGui::End();
        return;
    }

    const double now = ImGui::GetTime();
    if (ImPlot::BeginPlot("##topicplot", ImVec2(-1.0f, -1.0f))) {
        ImPlot::SetupAxes("t (s)", "value", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
        // Scroll the x-axis to always show the last historySeconds.
        ImPlot::SetupAxisLimits(ImAxis_X1, now - static_cast<double>(historySeconds_), now,
                                ImPlotCond_Always);
        for (const auto& ch : channels_) {
            const int n = static_cast<int>(ch.t.size());
            if (n > 0) {
                // Offset lets ImPlot draw across the ring buffer's wrap point (Stride
                // defaults to sizeof(float)).
                ImPlotSpec spec;
                spec.Offset = ch.offset;
                ImPlot::PlotLine(ch.name.c_str(), ch.t.data(), ch.v.data(), n, spec);
            }
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

} // namespace utsyn
