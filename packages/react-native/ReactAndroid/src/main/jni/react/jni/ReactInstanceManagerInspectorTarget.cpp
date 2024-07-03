/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ReactInstanceManagerInspectorTarget.h"
#include "SafeReleaseJniRef.h"

#include <fbjni/NativeRunnable.h>
#include <jsinspector-modern/InspectorFlags.h>

#include <optional>

using namespace facebook::jni;
using namespace facebook::react::jsinspector_modern;

namespace facebook::react {

void ReactInstanceManagerInspectorTarget::TargetDelegate::onReload() const {
  auto method = javaClassStatic()->getMethod<void()>("onReload");
  method(self());
}

void ReactInstanceManagerInspectorTarget::TargetDelegate::
    onSetPausedInDebuggerMessage(
        const OverlaySetPausedInDebuggerMessageRequest& request) const {
  auto method = javaClassStatic()->getMethod<void(local_ref<JString>)>(
      "onSetPausedInDebuggerMessage");
  method(self(), request.message ? make_jstring(*request.message) : nullptr);
}

jni::local_ref<jni::JMap<jstring, jstring>>
ReactInstanceManagerInspectorTarget::TargetDelegate::getMetadata() const {
  auto method = javaClassStatic()
                    ->getMethod<jni::local_ref<jni::JMap<jstring, jstring>>()>(
                        "getMetadata");
  return method(self());
}

ReactInstanceManagerInspectorTarget::ReactInstanceManagerInspectorTarget(
    jni::alias_ref<ReactInstanceManagerInspectorTarget::jhybridobject> jobj,
    jni::alias_ref<JExecutor::javaobject> executor,
    jni::alias_ref<
        ReactInstanceManagerInspectorTarget::TargetDelegate::javaobject>
        delegate)
    : delegate_(make_global(delegate)) {
  auto& inspectorFlags = InspectorFlags::getInstance();

  if (inspectorFlags.getFuseboxEnabled()) {
    inspectorTarget_ = HostTarget::create(
        *this,
        [javaExecutor =
             // Use a SafeReleaseJniRef because this lambda may be copied to
             // arbitrary threads.
         SafeReleaseJniRef(make_global(executor))](auto callback) mutable {
          auto jrunnable =
              JNativeRunnable::newObjectCxxArgs(std::move(callback));
          javaExecutor->execute(jrunnable);
        });

    inspectorPageId_ = getInspectorInstance().addPage(
        "React Native Bridge (Experimental)",
        /* vm */ "",
        [inspectorTarget =
             inspectorTarget_](std::unique_ptr<IRemoteConnection> remote)
            -> std::unique_ptr<ILocalConnection> {
          return inspectorTarget->connect(std::move(remote));
        },
        {.nativePageReloads = true, .prefersFuseboxFrontend = true});
  }
}

ReactInstanceManagerInspectorTarget::~ReactInstanceManagerInspectorTarget() {
  if (inspectorPageId_.has_value()) {
    getInspectorInstance().removePage(*inspectorPageId_);
  }
}

jni::local_ref<ReactInstanceManagerInspectorTarget::jhybriddata>
ReactInstanceManagerInspectorTarget::initHybrid(
    jni::alias_ref<jhybridobject> jobj,
    jni::alias_ref<JExecutor::javaobject> executor,
    jni::alias_ref<
        ReactInstanceManagerInspectorTarget::TargetDelegate::javaobject>
        delegate) {
  return makeCxxInstance(jobj, executor, delegate);
}

void ReactInstanceManagerInspectorTarget::sendDebuggerResumeCommand() {
  if (inspectorTarget_) {
    inspectorTarget_->sendCommand(HostCommand::DebuggerResume);
  } else {
    jni::throwNewJavaException(
        "java/lang/IllegalStateException",
        "Cannot send command while the Fusebox backend is not enabled");
  }
}

void ReactInstanceManagerInspectorTarget::registerNatives() {
  registerHybrid({
      makeNativeMethod(
          "initHybrid", ReactInstanceManagerInspectorTarget::initHybrid),
      makeNativeMethod(
          "sendDebuggerResumeCommand",
          ReactInstanceManagerInspectorTarget::sendDebuggerResumeCommand),
  });
}

jsinspector_modern::HostTargetMetadata
ReactInstanceManagerInspectorTarget::getMetadata() {
  auto getMethod = jni::JMap<jstring, jstring>::javaClassLocal()
                       ->getMethod<jobject(jobject)>("get");
  auto metadata = delegate_->getMetadata();

  auto getStringOptional = [&](const std::string& key) {
    auto result = getMethod(metadata, make_jstring(key).get());
    return result ? std::optional<std::string>(result->toString())
                  : std::nullopt;
  };

  return {
      .appIdentifier = getStringOptional("appIdentifier"),
      .deviceName = getStringOptional("deviceName"),
      .integrationName = "Android Bridge (ReactInstanceManagerInspectorTarget)",
      .platform = getStringOptional("platform"),
      .reactNativeVersion = getStringOptional("reactNativeVersion"),
  };
}

void ReactInstanceManagerInspectorTarget::onReload(
    const PageReloadRequest& /*request*/) {
  delegate_->onReload();
}

void ReactInstanceManagerInspectorTarget::onSetPausedInDebuggerMessage(
    const OverlaySetPausedInDebuggerMessageRequest& request) {
  delegate_->onSetPausedInDebuggerMessage(request);
}

HostTarget* ReactInstanceManagerInspectorTarget::getInspectorTarget() {
  return inspectorTarget_.get();
}

} // namespace facebook::react
