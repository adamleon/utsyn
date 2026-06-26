// RobotDescriptionPlugin — loads a robot from the URDF on /robot_description and
// animates it from /joint_states, rviz-style. Owns a RobotActor (the 3D robot,
// injected into the viewport scene via SceneManager) and a MessageMonitor (topic
// feedback). The panel's inspector drives the actor (manual sliders offline, or a
// "load sample / load broken" test). With ROS2 ON the latched /robot_description
// and /joint_states feed the same actor methods.

#include "RobotActor.hpp"

#include "app/Logger.hpp"
#include "plugins/IPlugin.hpp"
#include "widgets/MessageMonitor.hpp"

#include <imgui.h>

#include <optional>
#include <string>

#if defined(UTSYN_ROS2) && UTSYN_ROS2
#  include <sensor_msgs/msg/joint_state.hpp>
#  include <std_msgs/msg/string.hpp>
#endif

namespace utsyn {

class RobotDescriptionPlugin final : public IPlugin {
public:
    void initialize(PluginContext& ctx) override {
        ctx_ = &ctx;

        actor_.emplace(ctx.scene);
        actor_->onAttach(); // loads the built-in sample arm (visible offline)

        monitor_.emplace(ctx.broker);
        urdfRow_   = monitor_->addRow("Robot Description", "/robot_description");
        jointsRow_ = monitor_->addRow("Joint States", "/joint_states");

#if defined(UTSYN_ROS2) && UTSYN_ROS2
        monitor_->bind<std_msgs::msg::String>(
                urdfRow_, [this](const std_msgs::msg::String& m) { onUrdf(m); },
                QoSProfile::latched());
        monitor_->bind<sensor_msgs::msg::JointState>(
                jointsRow_, [this](const sensor_msgs::msg::JointState& m) { onJoints(m); },
                QoSProfile{});
#endif

        ctx.logger.info("RobotDescription: actor attached; monitoring /robot_description + /joint_states");
    }

    void onSceneUpdate(float deltaTime) override {
        if (actor_) {
            actor_->onUpdate(deltaTime);
        }
    }

    void onImGui() override {
        if (!open_) {
            return;
        }
        if (ImGui::Begin("Robot Description", &open_, ImGuiWindowFlags_NoCollapse)) {
#if !(defined(UTSYN_ROS2) && UTSYN_ROS2)
            ImGui::TextDisabled("ROS2 off - showing the built-in sample; use the sliders.");
            ImGui::Separator();
#endif
            if (actor_) {
                actor_->onInspector();
            }
            ImGui::Separator();
            if (monitor_) {
                monitor_->draw();
            }
        }
        ImGui::End();
    }

    void shutdown() override {
        if (ctx_) {
            ctx_->logger.info("RobotDescription: shutting down");
        }
        if (actor_) {
            actor_->onDetach(); // remove the robot while the scene is still alive
        }
        actor_.reset();
        monitor_.reset();
        ctx_ = nullptr;
    }

    std::string name() const override { return "Robot Description"; }
    std::string version() const override { return "0.1.0"; }

private:
#if defined(UTSYN_ROS2) && UTSYN_ROS2
    void onUrdf(const std_msgs::msg::String& m) {
        if (actor_) {
            actor_->loadUrdf(m.data);
        }
    }
    void onJoints(const sensor_msgs::msg::JointState& m) {
        if (actor_) {
            actor_->setJoints(m.name, m.position);
        }
    }
#endif

    PluginContext*                ctx_ = nullptr;
    std::optional<RobotActor>     actor_;
    std::optional<MessageMonitor> monitor_;
    MessageMonitor::RowId         urdfRow_ = 0;
    MessageMonitor::RowId         jointsRow_ = 0;
    bool                          open_ = true;
};

} // namespace utsyn

#if defined(_WIN32)
#  define UTSYN_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define UTSYN_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" UTSYN_PLUGIN_EXPORT utsyn::IPlugin* createPlugin() {
    return new utsyn::RobotDescriptionPlugin();
}

extern "C" UTSYN_PLUGIN_EXPORT void destroyPlugin(utsyn::IPlugin* plugin) {
    delete plugin;
}

extern "C" UTSYN_PLUGIN_EXPORT int utsynPluginAbiVersion() {
    return UTSYN_PLUGIN_ABI_VERSION;
}
