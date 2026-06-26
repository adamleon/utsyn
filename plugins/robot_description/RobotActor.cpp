#include "RobotActor.hpp"

#include "app/Logger.hpp"

#include <threepp/loaders/URDFLoader.hpp>
#include <threepp/math/MathUtils.hpp>
#include <threepp/objects/Robot.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

namespace utsyn {

namespace {

// Built-in primitive (no mesh files) sample so the whole parse->inject->articulate
// path works with ROS2 OFF: base -> revolute joint1 (Z) -> link1 -> revolute
// joint2 (Y) -> link2 -> prismatic joint3 (Z) -> tool. Two joint types on purpose,
// so the prismatic/identity-rotation path is exercised. URDF is Z-up == viewport.
const char* const kSampleUrdf = R"URDF(<?xml version="1.0"?>
<robot name="utsyn_sample_arm">
  <link name="base_link">
    <visual><geometry><cylinder radius="0.15" length="0.1"/></geometry>
      <material name="base"><color rgba="0.2 0.8 0.2 1"/></material></visual>
  </link>
  <link name="link1">
    <visual><origin xyz="0 0 0.25"/><geometry><box size="0.1 0.1 0.5"/></geometry>
      <material name="l1"><color rgba="0.85 0.55 0.2 1"/></material></visual>
  </link>
  <link name="link2">
    <visual><origin xyz="0 0 0.2"/><geometry><box size="0.08 0.08 0.4"/></geometry>
      <material name="l2"><color rgba="0.25 0.55 0.9 1"/></material></visual>
  </link>
  <link name="tool">
    <visual><geometry><sphere radius="0.06"/></geometry>
      <material name="t"><color rgba="0.9 0.25 0.25 1"/></material></visual>
  </link>
  <joint name="joint1" type="revolute">
    <parent link="base_link"/><child link="link1"/>
    <origin xyz="0 0 0.1"/><axis xyz="0 0 1"/>
    <limit lower="-1.57" upper="1.57" effort="1" velocity="1"/>
  </joint>
  <joint name="joint2" type="revolute">
    <parent link="link1"/><child link="link2"/>
    <origin xyz="0 0 0.5"/><axis xyz="0 1 0"/>
    <limit lower="-1.57" upper="1.57" effort="1" velocity="1"/>
  </joint>
  <joint name="joint3" type="prismatic">
    <parent link="link2"/><child link="tool"/>
    <origin xyz="0 0 0.4"/><axis xyz="0 0 1"/>
    <limit lower="0" upper="0.3" effort="1" velocity="1"/>
  </joint>
</robot>)URDF";

} // namespace

RobotActor::RobotActor(SceneManager& scene) noexcept : Actor(scene) {}

RobotActor::~RobotActor() = default;

void RobotActor::onAttach() {
    loadUrdf(kSampleUrdf);
}

void RobotActor::onDetach() {
    if (handle_) {
        scene().remove(handle_);
        handle_ = {};
    }
    robot_.reset();
}

void RobotActor::loadUrdf(std::string xml, std::string baseDir) {
    if (xml == lastUrdf_ && !loadError_) {
        return; // identical re-delivery (latched topic) — don't re-parse
    }
    pendingUrdf_ = std::move(xml);
    baseDir_     = std::move(baseDir);
    urdfDirty_   = true;
}

void RobotActor::setJoints(const std::vector<std::string>& names,
                           const std::vector<double>& positions) {
    stagedNames_     = names;
    stagedPositions_ = positions;
    jointsDirty_     = true;
    manualMode_      = false; // live data takes over from the sliders
}

void RobotActor::onUpdate(float /*deltaTime*/) {
    if (urdfDirty_) {
        rebuild();
        urdfDirty_ = false;
    }
    if (robot_ && (jointsDirty_ || manualMode_)) {
        applyTargets();
    }
}

void RobotActor::rebuild() {
    loadError_ = false;

    // Guard threepp's empty-<link> UB: Robot::finalize() dereferences links_.front()
    // unconditionally, so a well-formed <robot> with zero <link> crashes inside the
    // loader. Reject before parsing and keep the current robot.
    if (pendingUrdf_.find("<link") == std::string::npos) {
        Logger::instance().error("robot_description: URDF has no <link>; keeping previous robot");
        loadError_  = true;
        statusLine_ = "rejected: URDF has no <link>";
        return;
    }

    // Rewrite package:// mesh URIs to absolute on-disk paths so threepp's loader
    // can find them. The URDF may arrive from a different ROS install than the one
    // utsyn runs in, so we resolve against configurable search roots rather than a
    // baseDir/package.xml the loader could walk to. Primitive URDFs (the sample arm)
    // have no package:// and pass through unchanged.
    int               meshResolved   = 0;
    int               meshUnresolved = 0;
    const std::string urdfForParse =
            resolver_.rewriteUrdf(pendingUrdf_, &meshResolved, &meshUnresolved);

    threepp::URDFLoader            loader;
    std::shared_ptr<threepp::Robot> robot;
    try {
        // parse() is non-throwing for plain URDF (returns nullptr on bad XML); the
        // try/catch only guards the xacro path, whose throw contract is unverified.
        robot = loader.parse(baseDir_, urdfForParse);
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("robot_description: parse threw: ") + e.what());
        loadError_  = true;
        statusLine_ = "parse error (xacro?)";
        return;
    }
    if (!robot) {
        Logger::instance().error("robot_description: URDF parse failed; keeping previous robot");
        loadError_  = true;
        statusLine_ = "parse failed";
        return;
    }

    // Clean replace: remove the old robot BEFORE adding the new one (never two in
    // the scene at once). No robot->rotation change — URDF Z-up == viewport Z-up,
    // and identity rotation is also required for prismatic-axis correctness.
    if (handle_) {
        scene().remove(handle_);
        handle_ = {};
    }
    robot_  = robot;
    handle_ = scene().add(robot_);
    robot_->showColliders(showCollision_); // URDFs ship visual + collision; hide the
                                           // collision wireframe by default like rviz

    buildJointMap();
    const std::size_t dof = robot_->numDOF();
    targets_.assign(dof, 0.0f);
    haveTarget_.assign(dof, 0);
    manual_.assign(dof, 0.0f);

    lastUrdf_   = pendingUrdf_;
    lastNumDof_ = static_cast<int>(dof);
    char buf[128];
    if (meshUnresolved > 0) {
        // Some meshes couldn't be found — the robot loads but renders incomplete.
        // Flag it so a misconfigured UTSYN_PACKAGE_PATH is visible, not silent.
        std::snprintf(buf, sizeof(buf), "loaded: %d DOF (%d mesh unresolved)", lastNumDof_,
                      meshUnresolved);
    } else {
        std::snprintf(buf, sizeof(buf), "loaded: %d DOF", lastNumDof_);
    }
    statusLine_ = buf;
    Logger::instance().info(std::string("robot_description: ") + statusLine_);
    if (meshResolved > 0 || meshUnresolved > 0) {
        Logger::instance().info("robot_description: meshes " + std::to_string(meshResolved) +
                                " resolved, " + std::to_string(meshUnresolved) + " unresolved");
    }
}

void RobotActor::buildJointMap() {
    nameToDof_.clear();
    const auto info = robot_->getArticulatedJointInfo(); // size == numDOF, articulated order
    for (std::size_t i = 0; i < info.size(); ++i) {
        nameToDof_[info[i].name] = i;
    }
}

void RobotActor::applyTargets() {
    if (!robot_) {
        return;
    }
    if (manualMode_) {
        for (std::size_t i = 0; i < manual_.size(); ++i) {
            robot_->setJointValue(i, manual_[i], /*deg=*/false); // manual_ is native (rad/m)
        }
        return;
    }
    if (jointsDirty_) {
        const std::size_t n = std::min(stagedNames_.size(), stagedPositions_.size());
        for (std::size_t k = 0; k < n; ++k) {
            const auto it = nameToDof_.find(stagedNames_[k]);
            if (it == nameToDof_.end()) {
                continue; // joint not in this URDF — skip
            }
            const float p = static_cast<float>(stagedPositions_[k]);
            if (!std::isfinite(p)) {
                continue; // NaN/inf guard
            }
            targets_[it->second]    = p;
            haveTarget_[it->second] = 1;
        }
        jointsDirty_ = false;
    }
    for (std::size_t i = 0; i < targets_.size(); ++i) {
        if (haveTarget_[i]) {
            robot_->setJointValue(i, targets_[i], /*deg=*/false); // radians / meters
        }
    }
}

void RobotActor::onInspector() {
    ImGui::Text("URDF: %s", hasRobot() ? "loaded" : "none");
    if (loadError_) {
        ImGui::TextColored(ImVec4(0.80f, 0.27f, 0.20f, 1.0f), "%s", statusLine_.c_str());
    } else {
        ImGui::TextDisabled("%s", statusLine_.c_str());
    }
    ImGui::Text("DOF: %d", lastNumDof_);

    if (ImGui::Button("Reload sample")) {
        loadUrdf(kSampleUrdf);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load broken URDF")) {
        loadUrdf("<robot name=\"broken\"></robot>"); // negative test: no <link>
    }

    ImGui::Separator();
    if (ImGui::Checkbox("Show collision geometry", &showCollision_) && robot_) {
        robot_->showColliders(showCollision_);
    }
    if (ImGui::Checkbox("Manual joint control", &manualMode_) && manualMode_ && robot_) {
        // Seed the sliders from the robot's current pose so manual mode resumes from
        // wherever live data left it, instead of snapping back to stale manual values.
        const std::size_t n = std::min(manual_.size(), robot_->numDOF());
        for (std::size_t i = 0; i < n; ++i) {
            manual_[i] = robot_->getJointValue(i, /*deg=*/false);
        }
    }
    if (!robot_) {
        return;
    }

    const auto info = robot_->getArticulatedJointInfo();
    for (std::size_t i = 0; i < info.size() && i < manual_.size(); ++i) {
        const bool revolute = info[i].type == threepp::Robot::JointType::Revolute;
        const auto range    = robot_->getJointRange(i, /*deg=*/revolute);

        float lo = range.min;
        float hi = range.max;
        if (!std::isfinite(lo) || !std::isfinite(hi)) {
            lo = revolute ? -180.0f : -1.0f; // continuous joint — pick a sane span
            hi = revolute ? 180.0f : 1.0f;
        }

        // Show the manual target while editing; otherwise mirror the robot's live joint
        // value (driven by /joint_states) so the readout actually tracks the robot
        // instead of sitting at the never-updated manual value.
        float disp = manualMode_
                             ? (revolute ? threepp::math::radToDeg(manual_[i]) : manual_[i])
                             : robot_->getJointValue(i, /*deg=*/revolute);
        char  label[80];
        std::snprintf(label, sizeof(label), "%s (%s)##j%zu", info[i].name.c_str(),
                      revolute ? "deg" : "m", static_cast<std::size_t>(i));
        if (ImGui::SliderFloat(label, &disp, lo, hi)) {
            manual_[i]  = revolute ? threepp::math::degToRad(disp) : disp;
            manualMode_ = true; // editing a slider re-takes manual control
        }
    }
}

} // namespace utsyn
