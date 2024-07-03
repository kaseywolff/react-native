/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "JReactHostInspectorTarget.h"
#include <fbjni/NativeRunnable.h>
#include <jsinspector-modern/InspectorFlags.h>
#include <react/jni/SafeReleaseJniRef.h>

using namespace facebook::jni;
using namespace facebook::react::jsinspector_modern;

namespace facebook::react {
JReactHostInspectorTarget::JReactHostInspectorTarget(
    alias_ref<JReactHostImpl::javaobject> reactHostImpl,
    alias_ref<JExecutor::javaobject> executor)
    : javaReactHostImpl_(make_global(reactHostImpl)),
      javaExecutor_(make_global(executor)) {
  auto& inspectorFlags = InspectorFlags::getInstance();
  if (inspectorFlags.getFuseboxEnabled()) {
    inspectorTarget_ = HostTarget::create(
        *this,
        [javaExecutor =
             // Use a SafeReleaseJniRef because this lambda may be copied to
             // arbitrary threads.
         SafeReleaseJniRef(javaExecutor_)](
            std::function<void()>&& callback) mutable {
          auto jrunnable =
              JNativeRunnable::newObjectCxxArgs(std::move(callback));
          javaExecutor->execute(jrunnable);
        });

    inspectorPageId_ = getInspectorInstance().addPage(
        "React Native Bridgeless (Experimental)",
        /* vm */ "",
        [inspectorTargetWeak = std::weak_ptr(inspectorTarget_)](
            std::unique_ptr<IRemoteConnection> remote)
            -> std::unique_ptr<ILocalConnection> {
          if (auto inspectorTarget = inspectorTargetWeak.lock()) {
            return inspectorTarget->connect(std::move(remote));
          }
          // Reject the connection.
          return nullptr;
        },
        {.nativePageReloads = true, .prefersFuseboxFrontend = true});
  }
}

JReactHostInspectorTarget::~JReactHostInspectorTarget() {
  if (inspectorPageId_.has_value()) {
    getInspectorInstance().removePage(*inspectorPageId_);
  }
}

local_ref<JReactHostInspectorTarget::jhybriddata>
JReactHostInspectorTarget::initHybrid(
    alias_ref<JReactHostInspectorTarget::jhybridobject> self,
    jni::alias_ref<JReactHostImpl::javaobject> reactHostImpl,
    jni::alias_ref<JExecutor::javaobject> executor) {
  return makeCxxInstance(reactHostImpl, executor);
}

void JReactHostInspectorTarget::sendDebuggerResumeCommand() {
  if (inspectorTarget_) {
    inspectorTarget_->sendCommand(HostCommand::DebuggerResume);
  } else {
    jni::throwNewJavaException(
        "java/lang/IllegalStateException",
        "Cannot send command while the Fusebox backend is not enabled");
  }
}

void JReactHostInspectorTarget::registerNatives() {
  registerHybrid({
      makeNativeMethod("initHybrid", JReactHostInspectorTarget::initHybrid),
      makeNativeMethod(
          "sendDebuggerResumeCommand",
          JReactHostInspectorTarget::sendDebuggerResumeCommand),
  });
}

jsinspector_modern::HostTargetMetadata
JReactHostInspectorTarget::getMetadata() {
  jsinspector_modern::HostTargetMetadata metadata = {
      .integrationName = "Android Bridgeless (ReactHostImpl)",
  };

  if (auto javaReactHostImplStrong = javaReactHostImpl_->get()) {
    auto javaMetadata = javaReactHostImplStrong->getHostMetadata();
    auto getMethod = jni::JMap<jstring, jstring>::javaClassLocal()
                         ->getMethod<jobject(jobject)>("get");

    auto getStringOptional = [&](const std::string& key) {
      auto result = getMethod(javaMetadata, make_jstring(key).get());
      return result ? std::optional<std::string>(result->toString())
                    : std::nullopt;
    };

    metadata.appIdentifier = getStringOptional("appIdentifier");
    metadata.deviceName = getStringOptional("deviceName");
    metadata.platform = getStringOptional("platform");
    metadata.reactNativeVersion = getStringOptional("reactNativeVersion");
  }

  return metadata;
}

void JReactHostInspectorTarget::onReload(const PageReloadRequest& request) {
  javaReactHostImpl_->reload("CDP Page.reload");
}

void JReactHostInspectorTarget::onSetPausedInDebuggerMessage(
    const OverlaySetPausedInDebuggerMessageRequest& request) {
  javaReactHostImpl_->setPausedInDebuggerMessage(request.message);
}

HostTarget* JReactHostInspectorTarget::getInspectorTarget() {
  return inspectorTarget_ ? inspectorTarget_.get() : nullptr;
}
} // namespace facebook::react
