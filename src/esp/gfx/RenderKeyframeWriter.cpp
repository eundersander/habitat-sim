// Copyright (c) Facebook, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "Recorder.h"

#include "esp/io/JsonSerializeTypes.h"
#include "esp/io/json.h"

#include <rapidjson/document.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <iostream>

using namespace rapidjson;

namespace esp {
namespace gfx {

/**
 * @brief Helper class to get notified when a SceneNode is about to be destroyed.
 */
class NodeDeletionHelper : public Magnum::SceneGraph::AbstractFeature3D {
 public:
  NodeDeletionHelper(scene::SceneNode& node_, Recorder* writer)
      : Magnum::SceneGraph::AbstractFeature3D(node_),
        node(&node_),
        writer_(writer) {}

  virtual ~NodeDeletionHelper() { writer_->onDeleteRenderAssetInstance(node); }

 private:
  Recorder* writer_ = nullptr;
  const scene::SceneNode* node = nullptr;
};

Recorder::~Recorder() {
  // Delete NodeDeletionHelpers. This is important because they hold raw pointers
  // to this Recorder and these pointers would become dangling (invalid)
  // after this Recorder is destroyed.
  for (auto& instanceRecord : instanceRecords_) {
    delete instanceRecord.deletionHelper;
  }
}

void Recorder::onLoadRenderAsset(
    const esp::assets::AssetInfo& assetInfo) {
  getKeyframe().loads.push_back(assetInfo);
}

void Recorder::onCreateRenderAssetInstance(
    scene::SceneNode* node,
    const esp::assets::RenderAssetInstanceCreationInfo& creation) {
  ASSERT(node);
  ASSERT(findInstance(node) == -1);

  RenderAssetInstanceKey instanceKey = getNewInstanceKey();

  getKeyframe().creations.emplace_back(std::make_pair(instanceKey, creation));

  // Constructing NodeDeletionHelper here is equivalent to calling node->addFeature.
  // We keep a pointer to deletionHelper so we can delete it manually later if necessary.
  NodeDeletionHelper* deletionHelper = new NodeDeletionHelper{*node, this};

  instanceRecords_.emplace_back(
      RenderAssetInstanceRecord{node, instanceKey, Corrade::Containers::NullOpt, deletionHelper});
}

void Recorder::saveKeyframe() {
  updateInstanceStates();
  advanceKeyframe();
}

void Recorder::addUserTransformToKeyframe(
    const std::string& name,
    const Magnum::Vector3& translation,
    const Magnum::Quaternion& rotation) {
  getKeyframe().userTransforms[name] = Transform{translation, rotation};
}

void Recorder::addLoadsCreationsDeletions(
    RenderKeyframeIterator begin,
    RenderKeyframeIterator end,
    RenderKeyframe* dest) {
  ASSERT(dest);
  for (RenderKeyframeIterator curr = begin; curr != end; curr++) {
    const auto& keyframe = *curr;
    dest->loads.insert(dest->loads.end(), keyframe.loads.begin(),
                       keyframe.loads.end());
    dest->creations.insert(dest->creations.end(), keyframe.creations.begin(),
                           keyframe.creations.end());
    for (const auto& deletionInstanceKey : keyframe.deletions) {
      checkAndAddDeletion(dest, deletionInstanceKey);
    }
  }
}

void Recorder::checkAndAddDeletion(
    RenderKeyframe* keyframe,
    RenderAssetInstanceKey instanceKey) {
  auto it =
      std::find_if(keyframe->creations.begin(), keyframe->creations.end(),
                   [&](const auto& pair) { return pair.first == instanceKey; });
  if (it != keyframe->creations.end()) {
    // this deletion just cancels out with an earlier creation
    keyframe->creations.erase(it);
  } else {
    // This deletion has no matching creation so it can't be canceled out.
    // Include it in the keyframe.
    keyframe->deletions.push_back(instanceKey);
  }
}

void Recorder::onDeleteRenderAssetInstance(
    const scene::SceneNode* node) {
  int index = findInstance(node);
  ASSERT(index != -1);

  auto instanceKey = instanceRecords_[index].instanceKey;

  checkAndAddDeletion(&getKeyframe(), instanceKey);

  instanceRecords_.erase(instanceRecords_.begin() + index);
}

RenderKeyframe& Recorder::getKeyframe() {
  return currKeyframe_;
}

RenderAssetInstanceKey Recorder::getNewInstanceKey() {
  return nextInstanceKey_++;
}

int Recorder::findInstance(const scene::SceneNode* queryNode) {
  auto it = std::find_if(instanceRecords_.begin(), instanceRecords_.end(),
                         [&queryNode](const RenderAssetInstanceRecord& record) {
                           return record.node == queryNode;
                         });

  return it == instanceRecords_.end() ? -1 : int(it - instanceRecords_.begin());
}

RenderAssetInstanceState Recorder::getInstanceState(
    const scene::SceneNode* node) {
  const auto absTransformMat = node->absoluteTransformation();
  Transform absTransform{
    absTransformMat.translation(),
    Magnum::Quaternion::fromMatrix(absTransformMat.rotationShear())};

  return RenderAssetInstanceState{
      absTransform,
      node->getSemanticId()};
}

void Recorder::updateInstanceStates() {
  for (auto& instanceRecord : instanceRecords_) {
    auto state = getInstanceState(instanceRecord.node);
    if (!instanceRecord.recentState || state != instanceRecord.recentState) {
      getKeyframe().stateUpdates.push_back(
          std::make_pair(instanceRecord.instanceKey, state));
      instanceRecord.recentState = state;
    }
  }
}

void Recorder::advanceKeyframe() {
  savedKeyframes_.emplace_back(std::move(currKeyframe_));
  currKeyframe_ = RenderKeyframe{};
}

void Recorder::writeSavedKeyframesToFile(
    const std::string& filepath) {
  auto document = writeKeyframesToJsonDocument();
  esp::io::writeJsonToFile(document, filepath);

  // consolidate saved keyframes into current keyframe
  addLoadsCreationsDeletions(savedKeyframes_.begin(), savedKeyframes_.end(),
                             &getKeyframe());
  savedKeyframes_.clear();
}


rapidjson::Document Recorder::writeKeyframesToJsonDocument() {
  if (savedKeyframes_.empty()) {
    LOG(WARNING) << "Recorder::writeKeyframesToJsonDocument: no saved keyframes to write";
    return rapidjson::Document();
  }

  Document d;
  d.SetObject();
  Document::AllocatorType& allocator = d.GetAllocator();

  Value keyframesArray(kArrayType);
  for (const auto& keyframe : savedKeyframes_) {
    Value keyframeObj(kObjectType);

    if (!keyframe.loads.empty()) {
      Value loadsArray(kArrayType);
      for (const auto& assetInfo : keyframe.loads) {
        Value loadObj(kObjectType);
        esp::io::AddMember(loadObj, "assetInfo", assetInfo, allocator);
        loadsArray.PushBack(loadObj, allocator);
      }
      esp::io::AddMember(keyframeObj, "loads", loadsArray, allocator);
    }

    if (!keyframe.creations.empty()) {
      Value creationsArray(kArrayType);
      for (const auto& pair : keyframe.creations) {
        Value creationPairObj(kObjectType);
        esp::io::AddMember(creationPairObj, "instanceKey", pair.first,
                           allocator);
        esp::io::AddMember(creationPairObj, "creation", pair.second, allocator);

        creationsArray.PushBack(creationPairObj, allocator);
      }
      esp::io::AddMember(keyframeObj, "creations", creationsArray, allocator);
    }

    if (!keyframe.deletions.empty()) {
      Value deletionsArray(kArrayType);
      for (const auto& deletionInstanceKey : keyframe.deletions) {
        deletionsArray.PushBack(deletionInstanceKey, allocator);
      }
      esp::io::AddMember(keyframeObj, "deletions", deletionsArray, allocator);
    }

    if (!keyframe.stateUpdates.empty()) {
      Value stateUpdatesArray(kArrayType);
      for (const auto& pair : keyframe.stateUpdates) {
        const auto& state = pair.second;

        Value stateObj(kObjectType);
        esp::io::AddMember(stateObj, "instanceKey", pair.first, allocator);
        esp::io::AddMember(stateObj, "absTransform", state.absTransform,
                           allocator);
        esp::io::AddMember(stateObj, "semanticId", state.semanticId, allocator);

        stateUpdatesArray.PushBack(stateObj, allocator);
      }
      esp::io::AddMember(keyframeObj, "stateUpdates", stateUpdatesArray,
                         allocator);
    }

    if (!keyframe.userTransforms.empty()) {
      Value userTransformsArray(kArrayType);
      for (const auto& pair : keyframe.userTransforms) {
        Value wrapperObj(kObjectType);
        esp::io::AddMember(wrapperObj, "name", pair.first, allocator);
        esp::io::AddMember(wrapperObj, "transform", pair.second, allocator);
        userTransformsArray.PushBack(wrapperObj, allocator);
      }
      esp::io::AddMember(keyframeObj, "userTransforms", userTransformsArray,
                         allocator);
    }

#ifdef ENABLE_RENDER_KEYFRAME_OBSERVATIONS
    if (keyframe.observation) {
      const auto& obs = *keyframe.observation;
      Value obsObj(kObjectType);
      esp::io::AddMember(obsObj, "cameraTransform", obs.cameraTransform,
                         allocator);
      esp::io::AddMemberEnum(obsObj, "sensorType", obs.sensorType, allocator);
      esp::io::AddMember(keyframeObj, "observation", obsObj, allocator);
    }
#endif

    keyframesArray.PushBack(keyframeObj, allocator);
  }

  d.AddMember("keyframes", keyframesArray, allocator);
  return d;
}

}  // namespace gfx
}  // namespace esp
