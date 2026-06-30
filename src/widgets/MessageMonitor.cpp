#include "widgets/MessageMonitor.hpp"

#include "app/Theme.hpp"
#include "widgets/TerminalUi.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>

namespace utsyn {

namespace {

// Display state derived on the render thread from the ROS-thread TopicStats plus
// message age. The registry only ever reports Live/NotAdvertised/RosDisabled; the
// stale and latched-is-healthy judgments are made here.
enum class Disp { Off, Error, Waiting, Live, Stale, Latched };

double nowSteadySeconds() {
    return std::chrono::duration<double>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

Disp computeDisplay(const TopicStats& s, double now) {
    switch (s.state) {
        case StreamState::RosDisabled:
            return Disp::Off;
        case StreamState::TypeMismatch:
        case StreamState::QosIncompatible:
            return Disp::Error;
        case StreamState::NotAdvertised:
        case StreamState::AdvertisedNoData:
            return Disp::Waiting;
        case StreamState::Live:
        case StreamState::LatchedReceived:
            break;
    }
    // A message has been received. Decide live vs stale from age, but treat a quiet
    // latched (transient_local) topic as healthy rather than stale.
    const double age = (s.lastReceiptMono > 0.0) ? (now - s.lastReceiptMono) : 1.0e9;
    const double staleAfter =
            (s.rateHz > 1.0e-3) ? std::max(3.0 / s.rateHz, 0.5) : 1.0;
    if (age > staleAfter) {
        return s.latched ? Disp::Latched : Disp::Stale;
    }
    return Disp::Live;
}

// Brackets hug the word — no padding INSIDE the brackets (which read as stray
// spaces). Column alignment is handled by the caller, which prints the token in a
// fixed-width field so the labels still line up. A quiet-but-healthy latched topic
// reads [LATCH] (same healthy green as a live stream) so it's distinguished from a
// streaming [LIVE] at a glance, not collapsed into it.
const char* tokenFor(Disp d) {
    switch (d) {
        case Disp::Off:     return "[OFF]";
        case Disp::Error:   return "[ERROR]";
        case Disp::Waiting: return "[WAIT]";
        case Disp::Live:    return "[LIVE]";
        case Disp::Stale:   return "[STALE]";
        case Disp::Latched: return "[LATCH]";
    }
    return "[?]";
}

ImVec4 colorFor(Disp d) {
    switch (d) {
        case Disp::Live:
        case Disp::Latched: return theme::StatusLive;
        case Disp::Stale:   return theme::StatusStale;
        case Disp::Error:   return theme::StatusError;
        case Disp::Off:
        case Disp::Waiting: return theme::StatusIdle;
    }
    return theme::StatusIdle;
}

const char* stateName(const TopicStats& s) {
    switch (s.state) {
        case StreamState::RosDisabled:      return "ROS_DISABLED";
        case StreamState::NotAdvertised:    return "WAITING";
        case StreamState::AdvertisedNoData: return "NO_DATA";
        case StreamState::Live:             return "LIVE";
        case StreamState::LatchedReceived:  return "LATCHED";
        case StreamState::TypeMismatch:     return "TYPE_MISMATCH";
        case StreamState::QosIncompatible:  return "QOS_INCOMPATIBLE";
    }
    return "?";
}

// Right-hand summary shown on the header line.
void rightText(char* buf, std::size_t n, Disp d, const TopicStats& s, double now) {
    const double age = (s.lastReceiptMono > 0.0) ? (now - s.lastReceiptMono) : 0.0;
    switch (d) {
        case Disp::Live:
            std::snprintf(buf, n, "%6.1f Hz  %4.2fs", s.rateHz, age);
            break;
        case Disp::Latched:
            std::snprintf(buf, n, "latched  %.1fs", age);
            break;
        case Disp::Stale:
            std::snprintf(buf, n, "STALE %.1fs", age);
            break;
        case Disp::Waiting:
            std::snprintf(buf, n, "waiting");
            break;
        case Disp::Error:
            std::snprintf(buf, n, "no publisher / type");
            break;
        case Disp::Off:
            std::snprintf(buf, n, "ROS2 off");
            break;
    }
}

} // namespace

MessageMonitor::~MessageMonitor() {
    // Best-effort release. At app teardown the registry is already cleared, so
    // these resolve to no-ops; this matters only for a monitor destroyed at runtime.
    for (Row& r : rows_) {
        if (r.handle != kInvalidTopic) {
            broker_->release(r.handle);
            r.handle = kInvalidTopic;
        }
    }
}

MessageMonitor::RowId MessageMonitor::addRow(std::string label, std::string topic,
                                             bool userRemovable) {
    Row r;
    r.label = std::move(label);
    r.topic = std::move(topic);
    r.userRemovable = userRemovable;
    const std::size_t copyLen = std::min(r.topic.size(), r.topicEdit.size() - 1);
    std::copy_n(r.topic.begin(), copyLen, r.topicEdit.begin());
    r.topicEdit[copyLen] = '\0';
    rows_.push_back(std::move(r));
    return rows_.size() - 1;
}

void MessageMonitor::setTopic(RowId row, std::string topic) {
    if (row >= rows_.size()) {
        return;
    }
    Row& r = rows_[row];
    r.topic = std::move(topic);
    const std::size_t copyLen = std::min(r.topic.size(), r.topicEdit.size() - 1);
    std::copy_n(r.topic.begin(), copyLen, r.topicEdit.begin());
    r.topicEdit[copyLen] = '\0';
    if (r.resubscribe) {
        r.handle = r.resubscribe(r.handle, r.topic); // releases old, subscribes new
    }
}

void MessageMonitor::remove(RowId row) {
    if (row >= rows_.size()) {
        return;
    }
    Row& r = rows_[row];
    if (r.handle != kInvalidTopic) {
        broker_->release(r.handle);
        r.handle = kInvalidTopic;
    }
    r.removed = true;
}

bool MessageMonitor::isLive(RowId row) const {
    if (row >= rows_.size()) {
        return false;
    }
    return computeDisplay(broker_->stats(rows_[row].handle), nowSteadySeconds()) ==
           Disp::Live;
}

double MessageMonitor::rateHz(RowId row) const {
    if (row >= rows_.size()) {
        return 0.0;
    }
    return broker_->stats(rows_[row].handle).rateHz;
}

void MessageMonitor::draw() {
    const double now = nowSteadySeconds();

    // Panel-level roll-up, tinted by the worst state present.
    int live = 0, stale = 0, other = 0;
    Disp worst = Disp::Off;
    for (const Row& r : rows_) {
        if (r.removed) {
            continue;
        }
        const Disp d = computeDisplay(broker_->stats(r.handle), now);
        if (d == Disp::Live || d == Disp::Latched) {
            ++live;
        } else if (d == Disp::Stale || d == Disp::Error) {
            ++stale;
            worst = Disp::Stale;
        } else {
            ++other;
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, colorFor(stale > 0 ? worst : Disp::Live));
    ImGui::Text("MONITORS  %d live / %d degraded / %d idle", live, stale, other);
    ImGui::PopStyleColor();
    ui::dashRule();

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].removed) {
            drawRow(rows_[i], i);
        }
    }
}

void MessageMonitor::drawRow(Row& row, RowId id) {
    const double now = nowSteadySeconds();
    const TopicStats stats = broker_->stats(row.handle);
    const Disp disp = computeDisplay(stats, now);
    const ImVec4 col = colorFor(disp);

    // Feed the rate sparkline (zero unless live).
    row.rateHistory[row.rateHead] =
            (disp == Disp::Live) ? static_cast<float>(stats.rateHz) : 0.0f;
    row.rateHead = (row.rateHead + 1) % kRateHistory;

    char right[64];
    rightText(right, sizeof(right), disp, stats, now);

    // The whole status line is one colored, full-width clickable row with an ASCII
    // [+]/[-] marker (not ImGui's vector arrow). The "###" gives a stable id (the
    // row index) so the dynamic text never resets the open/closed state.
    // %-7s on the token left-justifies it in a 7-wide field (the widest token,
    // "[STALE]"/"[ERROR]", is 7) so the padding lands AFTER the "]" as column space,
    // never inside the brackets — keeping every token tight while the labels align.
    char header[320];
    std::snprintf(header, sizeof(header), "%s %-7s  %-22s  %s###mmrow%zu",
                  ui::collapseMarker(row.open), tokenFor(disp), row.label.c_str(),
                  right, static_cast<std::size_t>(id));

    ImGui::PushID(static_cast<int>(id));
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ui::collapsibleRow(row.open, header);
    ImGui::PopStyleColor();

    if (row.open) {
        ImGui::Indent();

        // Key/value grid.
        ImGui::TextDisabled("TOPIC "); ImGui::SameLine();
        ImGui::TextUnformatted(row.topic.c_str());
        ImGui::TextDisabled("STATE "); ImGui::SameLine();
        ImGui::TextUnformatted(stateName(stats));
        ImGui::TextDisabled("RATE  "); ImGui::SameLine();
        if (disp == Disp::Live) {
            ImGui::Text("%.1f Hz", stats.rateHz);
        } else {
            ImGui::TextUnformatted("--");
        }
        ImGui::TextDisabled("COUNT "); ImGui::SameLine();
        ImGui::Text("%llu", static_cast<unsigned long long>(stats.messageCount));
        ImGui::TextDisabled("LAST  "); ImGui::SameLine();
        if (stats.lastReceiptMono > 0.0) {
            ImGui::Text("%.2fs ago%s", now - stats.lastReceiptMono,
                        stats.latched ? " (latched)" : "");
        } else {
            ImGui::TextUnformatted("never");
        }
        ImGui::TextDisabled("SIZE  "); ImGui::SameLine();
        if (stats.lastMsgBytes > 0) {
            ImGui::Text("%u B", stats.lastMsgBytes);
        } else {
            ImGui::TextUnformatted("--");
        }

        // Rate sparkline (built into ImGui; no ImPlot dependency in the widget).
        const float plotH = ImGui::GetFontSize() * 2.5f;
        ImGui::PlotLines("##rate", row.rateHistory.data(),
                         static_cast<int>(kRateHistory),
                         static_cast<int>(row.rateHead), nullptr, 0.0f, FLT_MAX,
                         ImVec2(0.0f, plotH));

        // Editable topic — the user can retarget which topic this row watches. The
        // leading ">" reads as a terminal prompt.
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
        ImGui::InputText("##topic", row.topicEdit.data(), row.topicEdit.size());
        ImGui::SameLine();
        if (ImGui::Button("set")) {
            setTopic(id, std::string(row.topicEdit.data()));
        }
        if (row.userRemovable) {
            ImGui::SameLine();
            if (ImGui::Button("del")) {
                remove(id);
            }
        }

        ImGui::Unindent();
    }

    ImGui::PopID();
}

} // namespace utsyn
