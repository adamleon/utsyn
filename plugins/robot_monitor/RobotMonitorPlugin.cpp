// RobotMonitorPlugin — demo plugin that proves the topic-monitoring base, and the
// seed of the eventual robot_description plugin. It monitors the two topics a
// robot display needs — /joint_states and the latched /robot_description — and
// shows live streaming feedback via the reusable MessageMonitor widget.
//
// With ROS2 OFF the plugin still loads and the rows render as ROS-disabled, so the
// widget can be exercised without a ROS graph. With ROS2 ON the rows light up with
// rate/age/latched feedback and the callbacks stash the latest payloads.

#include "app/Logger.hpp"
#include "app/PanelRegistry.hpp"
#include "plugins/IPlugin.hpp"
#include "widgets/MessageMonitor.hpp"
#include "widgets/TerminalUi.hpp"

#include <imgui.h>

#include <optional>
#include <string>

#if defined(UTSYN_ROS2) && UTSYN_ROS2
#  include <sensor_msgs/msg/joint_state.hpp>
#  include <std_msgs/msg/string.hpp>
#endif

namespace utsyn {

class RobotMonitorPlugin final : public IPlugin {
public:
    void initialize(PluginContext& ctx) override {
        ctx_   = &ctx;
        panel_ = ctx.panels.add("Robot Monitor", name()); // View-menu entry
        monitor_.emplace(ctx.broker);

        // Predefined rows. addRow creates the display row regardless of ROS2; bind
        // attaches the real subscription only when ROS2 is present.
        jointsRow_ = monitor_->addRow("Joint States", "/joint_states");
        urdfRow_   = monitor_->addRow("Robot Description", "/robot_description");

#if defined(UTSYN_ROS2) && UTSYN_ROS2
        monitor_->bind<sensor_msgs::msg::JointState>(
                jointsRow_,
                [this](const sensor_msgs::msg::JointState& m) { onJoints(m); },
                QoSProfile{});
        monitor_->bind<std_msgs::msg::String>(
                urdfRow_,
                [this](const std_msgs::msg::String& m) { onUrdf(m); },
                QoSProfile::latched()); // /robot_description is transient_local
#endif

        ctx.logger.info("RobotMonitor: monitoring /joint_states and /robot_description");
    }

    void onImGui() override {
        if (!panel_->open) {
            return;
        }
        if (ImGui::Begin("Robot Monitor", &panel_->open, ImGuiWindowFlags_NoCollapse)) {
#if !(defined(UTSYN_ROS2) && UTSYN_ROS2)
            ImGui::TextDisabled("Built without ROS2 - topics are inert (rows show OFF).");
            ui::dashRule();
#endif
            if (monitor_) {
                monitor_->draw();
            }
            ui::dashRule();
            ImGui::Text("Last joint state: %d joint(s)", lastJointCount_);
            ImGui::Text("URDF received: %s (%d bytes)",
                        urdfReceived_ ? "yes" : "no", urdfBytes_);
        }
        ImGui::End();
    }

    void onSceneUpdate(float /*deltaTime*/) override {
        // TODO(robot_description): parse the URDF and drive joint transforms here.
    }

    void shutdown() override {
        if (ctx_) {
            ctx_->logger.info("RobotMonitor: shutting down");
        }
        monitor_.reset(); // releases broker handles while the registry is still intact
        ctx_ = nullptr;
    }

    std::string name() const override { return "Robot Monitor"; }
    std::string version() const override { return "0.1.0"; }

private:
#if defined(UTSYN_ROS2) && UTSYN_ROS2
    void onJoints(const sensor_msgs::msg::JointState& m) {
        lastJointCount_ = static_cast<int>(m.name.size());
    }
    void onUrdf(const std_msgs::msg::String& m) {
        urdfReceived_ = true;
        urdfBytes_ = static_cast<int>(m.data.size());
    }
#endif

    PluginContext*                ctx_ = nullptr;
    Panel*                        panel_ = nullptr; // owned by the app's PanelRegistry
    std::optional<MessageMonitor> monitor_;
    MessageMonitor::RowId         jointsRow_ = 0;
    MessageMonitor::RowId         urdfRow_ = 0;
    int                           lastJointCount_ = 0;
    int                           urdfBytes_ = 0;
    bool                          urdfReceived_ = false;
};

} // namespace utsyn

#if defined(_WIN32)
#  define UTSYN_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define UTSYN_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" UTSYN_PLUGIN_EXPORT utsyn::IPlugin* createPlugin() {
    return new utsyn::RobotMonitorPlugin();
}

extern "C" UTSYN_PLUGIN_EXPORT void destroyPlugin(utsyn::IPlugin* plugin) {
    delete plugin;
}

extern "C" UTSYN_PLUGIN_EXPORT int utsynPluginAbiVersion() {
    return UTSYN_PLUGIN_ABI_VERSION;
}
