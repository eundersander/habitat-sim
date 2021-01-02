// Copyright (c) Facebook, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <emscripten/bind.h>

#include <Magnum/EigenIntegration/GeometryIntegration.h>
#include <Magnum/EigenIntegration/Integration.h>

namespace em = emscripten;

#include "esp/gfx/replay/Player.h"
#include "esp/gfx/replay/ReplayManager.h"
#include "esp/scene/SemanticScene.h"
#include "esp/sensor/CameraSensor.h"
#include "esp/sim/Simulator.h"

using namespace esp;
using namespace esp::agent;
using namespace esp::core;
using namespace esp::geo;
using namespace esp::gfx;
using namespace esp::gfx::replay;
using namespace esp::nav;
using namespace esp::scene;
using namespace esp::sensor;
using namespace esp::sim;

// Consider
// https://becominghuman.ai/passing-and-returning-webassembly-array-parameters-a0f572c65d97
em::val Observation_getData(Observation& obs) {
  auto buffer = obs.buffer;
  if (buffer != nullptr) {
    return em::val(
        em::typed_memory_view(buffer->data.size(), buffer->data.data()));
  } else {
    return em::val::undefined();
  }
}

ObservationSpace Simulator_getAgentObservationSpace(Simulator& sim,
                                                    int agentId,
                                                    std::string sensorId) {
  ObservationSpace space;
  sim.getAgentObservationSpace(agentId, sensorId, space);
  return space;
}

std::map<std::string, ObservationSpace> Simulator_getAgentObservationSpaces(
    Simulator& sim,
    int agentId) {
  std::map<std::string, ObservationSpace> spaces;
  sim.getAgentObservationSpaces(agentId, spaces);
  return spaces;
}

Observation Sensor_getObservation(Sensor& sensor, Simulator& sim) {
  Observation ret;
  if (CameraSensor * camera{dynamic_cast<CameraSensor*>(&sensor)})
    camera->getObservation(sim, ret);
  return ret;
}

void Sensor_setLocalTransform(Sensor& sensor,
                              const vec3f& pos,
                              const vec4f& rot) {
  SceneNode& node{sensor.node()};

  node.resetTransformation();
  node.translate(Magnum::Vector3(pos));
  node.setRotation(Magnum::Quaternion(quatf(rot)).normalized());
}

vec3f quaternionToEuler(const quatf& q) {
  return q.toRotationMatrix().eulerAngles(0, 1, 2);
}

vec4f eulerToQuaternion(const vec3f& q) {
  return (Eigen::AngleAxisf(q.x(), vec3f::UnitX()) *
          Eigen::AngleAxisf(q.y(), vec3f::UnitY()) *
          Eigen::AngleAxisf(q.z(), vec3f::UnitZ()))
      .coeffs();
}

vec4f quaternionMultiply(const vec4f& q1, const vec4f& q2) {
  auto product = Magnum::Quaternion(quatf(q1)) * Magnum::Quaternion(quatf(q2));
  return vec4f(product.data()[0], product.data()[1], product.data()[2],
               product.data()[3]);
}

EMSCRIPTEN_BINDINGS(habitat_sim_bindings_js) {
  em::function("quaternionToEuler", &quaternionToEuler);
  em::function("eulerToQuaternion", &eulerToQuaternion);
  em::function("quaternionMultiply", &quaternionMultiply);

  em::register_vector<SensorSpec::ptr>("VectorSensorSpec");
  em::register_vector<size_t>("VectorSizeT");
  em::register_vector<std::string>("VectorString");
  em::register_vector<std::shared_ptr<SemanticCategory>>(
      "VectorSemanticCategories");
  em::register_vector<std::shared_ptr<SemanticObject>>("VectorSemanticObjects");

  em::register_map<std::string, float>("MapStringFloat");
  em::register_map<std::string, std::string>("MapStringString");
  em::register_map<std::string, Sensor::ptr>("MapStringSensor");
  em::register_map<std::string, SensorSpec::ptr>("MapStringSensorSpec");
  em::register_map<std::string, Observation>("MapStringObservation");
  em::register_map<std::string, ActionSpec::ptr>("ActionSpace");

  em::value_array<vec2f>("vec2f")
      .element(em::index<0>())
      .element(em::index<1>());

  em::value_array<vec3f>("vec3f")
      .element(em::index<0>())
      .element(em::index<1>())
      .element(em::index<2>());

  em::value_array<vec4f>("vec4f")
      .element(em::index<0>())
      .element(em::index<1>())
      .element(em::index<2>())
      .element(em::index<3>());

  em::value_array<vec2i>("vec2i")
      .element(em::index<0>())
      .element(em::index<1>());

  em::value_array<vec3i>("vec3i")
      .element(em::index<0>())
      .element(em::index<1>())
      .element(em::index<2>());

  em::value_array<vec4i>("vec4i")
      .element(em::index<0>())
      .element(em::index<1>())
      .element(em::index<2>())
      .element(em::index<3>());

  em::value_object<std::pair<vec3f, vec3f>>("aabb")
      .field("min", &std::pair<vec3f, vec3f>::first)
      .field("max", &std::pair<vec3f, vec3f>::second);

  em::class_<AgentConfiguration>("AgentConfiguration")
      .smart_ptr_constructor("AgentConfiguration",
                             &AgentConfiguration::create<>)
      .property("height", &AgentConfiguration::height)
      .property("radius", &AgentConfiguration::radius)
      .property("mass", &AgentConfiguration::mass)
      .property("linearAcceleration", &AgentConfiguration::linearAcceleration)
      .property("angularAcceleration", &AgentConfiguration::angularAcceleration)
      .property("linearFriction", &AgentConfiguration::linearFriction)
      .property("angularFriction", &AgentConfiguration::angularFriction)
      .property("coefficientOfRestitution",
                &AgentConfiguration::coefficientOfRestitution)
      .property("sensorSpecifications",
                &AgentConfiguration::sensorSpecifications);

  em::class_<ActionSpec>("ActionSpec")
      .smart_ptr_constructor(
          "ActionSpec",
          &ActionSpec::create<const std::string&, const ActuationMap&>)
      .property("name", &ActionSpec::name)
      .property("actuation", &ActionSpec::actuation);

  em::class_<PathFinder>("PathFinder")
      .smart_ptr<PathFinder::ptr>("PathFinder::ptr")
      .property("bounds", &PathFinder::bounds)
      .function("isNavigable", &PathFinder::isNavigable);

  em::class_<SensorSuite>("SensorSuite")
      .smart_ptr_constructor("SensorSuite", &SensorSuite::create<>)
      .function("get", &SensorSuite::get);

  em::enum_<SensorType>("SensorType")
      .value("NONE", SensorType::NONE)
      .value("COLOR", SensorType::COLOR)
      .value("DEPTH", SensorType::DEPTH)
      .value("NORMAL", SensorType::NORMAL)
      .value("SEMANTIC", SensorType::SEMANTIC)
      .value("PATH", SensorType::PATH)
      .value("GOAL", SensorType::GOAL)
      .value("FORCE", SensorType::FORCE)
      .value("TENSOR", SensorType::TENSOR)
      .value("TEXT", SensorType::TEXT);

  em::class_<SensorSpec>("SensorSpec")
      .smart_ptr_constructor("SensorSpec", &SensorSpec::create<>)
      .property("uuid", &SensorSpec::uuid)
      .property("sensorType", &SensorSpec::sensorType)
      .property("sensorSubtype", &SensorSpec::sensorSubType)
      .property("position", &SensorSpec::position)
      .property("orientation", &SensorSpec::orientation)
      .property("resolution", &SensorSpec::resolution)
      .property("channels", &SensorSpec::channels)
      .property("parameters", &SensorSpec::parameters);

  em::class_<Sensor>("Sensor")
      .smart_ptr<Sensor::ptr>("Sensor::ptr")
      .function("getObservation", &Sensor_getObservation)
      .function("setLocalTransform", &Sensor_setLocalTransform)
      .function("specification", &Sensor::specification);

  em::class_<SimulatorConfiguration>("SimulatorConfiguration")
      .smart_ptr_constructor("SimulatorConfiguration",
                             &SimulatorConfiguration::create<>)
      .property("scene_id", &SimulatorConfiguration::activeSceneID)
      .property("defaultAgentId", &SimulatorConfiguration::defaultAgentId)
      .property("defaultCameraUuid", &SimulatorConfiguration::defaultCameraUuid)
      .property("gpuDeviceId", &SimulatorConfiguration::gpuDeviceId)
      .property("compressTextures", &SimulatorConfiguration::compressTextures)
      .property("enableGfxReplaySave",
                &SimulatorConfiguration::enableGfxReplaySave);

  em::class_<AgentState>("AgentState")
      .smart_ptr_constructor("AgentState", &AgentState::create<>)
      .property("position", &AgentState::position)
      .property("rotation", &AgentState::rotation)
      .property("velocity", &AgentState::velocity)
      .property("angularVelocity", &AgentState::angularVelocity)
      .property("force", &AgentState::force)
      .property("torque", &AgentState::torque);

  em::class_<Agent>("Agent")
      .smart_ptr<Agent::ptr>("Agent::ptr")
      .property("config",
                em::select_overload<const AgentConfiguration&() const>(
                    &Agent::getConfig))
      .property("sensorSuite", em::select_overload<const SensorSuite&() const>(
                                   &Agent::getSensorSuite))
      .function("getState", &Agent::getState)
      .function("setState", &Agent::setState)
      .function("hasAction", &Agent::hasAction)
      .function("act", &Agent::act);

  em::class_<Observation>("Observation")
      .smart_ptr_constructor("Observation", &Observation::create<>)
      .function("getData", &Observation_getData);

  em::class_<ObservationSpace>("ObservationSpace")
      .smart_ptr_constructor("ObservationSpace", &ObservationSpace::create<>)
      .property("dataType", &ObservationSpace::dataType)
      .property("shape", &ObservationSpace::shape);

  em::class_<SemanticCategory>("SemanticCategory")
      .smart_ptr<SemanticCategory::ptr>("SemanticCategory::ptr")
      .function("getIndex", &SemanticCategory::index)
      .function("getName", &SemanticCategory::name);

  em::class_<SemanticObject>("SemanticObject")
      .smart_ptr<SemanticObject::ptr>("SemanticObject::ptr")
      .property("category", &SemanticObject::category);

  em::class_<SemanticScene>("SemanticScene")
      .smart_ptr<SemanticScene::ptr>("SemanticScene::ptr")
      .property("categories", &SemanticScene::categories)
      .property("objects", &SemanticScene::objects);

  em::class_<Player>("Player")
      .smart_ptr<Player::ptr>("Player::ptr")
      .property("numKeyframes", &Player::getNumKeyframes)
      // setKeyframeIndex is an expensive call, so we use explicit getter/setter
      // instead of a property.
      .function("setKeyframeIndex", &Player::setKeyframeIndex)
      .function("getKeyframeIndex", &Player::getKeyframeIndex);

  em::class_<ReplayManager>("ReplayManager")
      .smart_ptr<ReplayManager::ptr>("ReplayManager::ptr")
      .function("saveKeyframe", em::optional_override([](ReplayManager& self) {
                  if (!self.getRecorder()) {
                    LOG(ERROR) << "saveKeyframe: not enabled. See "
                                  "SimulatorConfiguration::"
                                  "enableGfxReplaySave.";
                    return;
                  }
                  self.getRecorder()->saveKeyframe();
                }))
      .function("addUserTransformToKeyframe",
                em::optional_override(
                    [](ReplayManager& self, const std::string& name,
                       const vec3f& translation, const vec4f& rotation) {
                      if (!self.getRecorder()) {
                        LOG(ERROR)
                            << "addUserTransformToKeyframe: not enabled. See "
                               "SimulatorConfiguration::"
                               "enableGfxReplaySave.";
                        return;
                      }
                      self.getRecorder()->addUserTransformToKeyframe(
                          name, Mn::Vector3(translation),
                          Magnum::Quaternion(quatf(rotation)));
                    }))
      .function("writeSavedKeyframesToString",
                em::optional_override([](ReplayManager& self) {
                  if (!self.getRecorder()) {
                    LOG(ERROR)
                        << "writeSavedKeyframesToString: not enabled. See "
                           "SimulatorConfiguration::enableGfxReplaySave.";
                    return std::string();
                  }
                  return self.getRecorder()->writeSavedKeyframesToString();
                }))
      .function("readKeyframesFromFile", &ReplayManager::readKeyframesFromFile);

  em::class_<Simulator>("Simulator")
      .smart_ptr_constructor("Simulator",
                             &Simulator::create<const SimulatorConfiguration&>)
      .function("getSemanticScene", &Simulator::getSemanticScene)
      .function("getGfxReplayManager", &Simulator::getGfxReplayManager)
      .function("seed", &Simulator::seed)
      .function("reconfigure", &Simulator::reconfigure)
      .function("reset", &Simulator::reset)
      .function("getAgentObservations", &Simulator::getAgentObservations)
      .function("getAgentObservation", &Simulator::getAgentObservation)
      .function("displayObservation", &Simulator::displayObservation)
      .function("getAgentObservationSpaces",
                &Simulator_getAgentObservationSpaces)
      .function("getAgentObservationSpace", &Simulator_getAgentObservationSpace)
      .function("getAgent", &Simulator::getAgent)
      .function("getPathFinder", &Simulator::getPathFinder)
      .function("addAgent",
                em::select_overload<Agent::ptr(const AgentConfiguration&)>(
                    &Simulator::addAgent))
      .function("addAgentToNode",
                em::select_overload<Agent::ptr(const AgentConfiguration&,
                                               scene::SceneNode&)>(
                    &Simulator::addAgent));
}
