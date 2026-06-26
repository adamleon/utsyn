#pragma once

#include "rendering/Actor.hpp"
#include "rendering/SceneManager.hpp"
#include "ros/PackageResolver.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace threepp {
class Robot;
}

namespace utsyn {

// A robot loaded from a URDF string and articulated from joint values. Uses
// threepp's URDFLoader (string -> Robot) + Robot::setJointValue. The Robot is a
// threepp::Object3D injected into the scene via SceneManager (one shared threepp
// instance — see CMake). Offline (no ROS2) it loads a built-in sample arm and is
// driven by manual sliders in the inspector; with ROS2 the plugin feeds it the
// latched /robot_description and /joint_states.
class RobotActor : public Actor {
public:
    explicit RobotActor(SceneManager& scene) noexcept;
    ~RobotActor() override;

    void onAttach() override;          // loads the built-in sample URDF
    void onUpdate(float deltaTime) override;
    void onDetach() override;
    void onInspector() override;
    [[nodiscard]] std::string name() const override { return "Robot"; }

    // Replace the current robot from a URDF (or xacro) string. No-op if identical
    // to the current one. Render-thread only.
    void loadUrdf(std::string xml, std::string baseDir = {});

    // Stage a /joint_states-style update: positions are radians (revolute) /
    // meters (prismatic), keyed by joint name. Switches off manual mode.
    void setJoints(const std::vector<std::string>& names,
                   const std::vector<double>& positions);

    [[nodiscard]] std::size_t numDof() const noexcept { return targets_.size(); }
    [[nodiscard]] bool        hasRobot() const noexcept { return static_cast<bool>(robot_); }

private:
    void rebuild();
    void buildJointMap();
    void applyTargets();

    std::shared_ptr<threepp::Robot>              robot_;   // also referenced by SceneManager
    SceneHandle                                  handle_;
    PackageResolver                              resolver_;     // package:// -> abs mesh paths
    std::unordered_map<std::string, std::size_t> nameToDof_;
    std::vector<float>                           targets_;     // last commanded value per DOF
    std::vector<std::uint8_t>                    haveTarget_;   // whether a DOF has been commanded
    std::vector<float>                           manual_;       // inspector slider value per DOF

    std::string pendingUrdf_;
    std::string baseDir_;
    std::string lastUrdf_;
    bool        urdfDirty_     = false;
    bool        jointsDirty_   = false;
    bool        manualMode_    = true;   // sliders drive until live joint data arrives
    bool        showCollision_ = false;  // hide collision wireframe by default (rviz-style)

    std::vector<std::string> stagedNames_;
    std::vector<double>      stagedPositions_;

    int         lastNumDof_ = 0;
    bool        loadError_  = false;
    std::string statusLine_;
};

} // namespace utsyn
