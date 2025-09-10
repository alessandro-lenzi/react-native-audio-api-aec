#pragma once

#include <jsi/jsi.h>

#include <audioapi/core/sources/AudioBuffer.h>
#include <audioapi/HostObjects/AudioBufferHostObject.h>
#include <audioapi/core/inputs/AudioRecorder.h>
#include <audioapi/HostObjects/RecorderAdapterNodeHostObject.h>

#ifdef ANDROID
#include <audioapi/android/core/AndroidAudioRecorder.h>
#include <audioapi/android/core/AndroidAudioRecorderWithAEC.h>
#else
#include <audioapi/ios/core/IOSAudioRecorder.h>
#include <audioapi/ios/core/IOSAudioRecorderWithAEC.h>
#endif

#include <memory>
#include <utility>
#include <vector>
#include <cstdio>

namespace audioapi {
using namespace facebook;

class AudioRecorderHostObject : public JsiHostObject {
 public:
  explicit AudioRecorderHostObject(
      jsi::Runtime *runtime,
      const std::shared_ptr<AudioEventHandlerRegistry> &audioEventHandlerRegistry,
      float sampleRate,
      int bufferLength,
      bool enableAEC = false,
      jobject audioManager = nullptr,
      jobject context = nullptr) {
#ifdef ANDROID
    if (enableAEC && audioManager != nullptr && context != nullptr) {
      audioRecorder_ = std::make_shared<AndroidAudioRecorderWithAEC>(
        sampleRate,
        bufferLength,
        audioEventHandlerRegistry,
        audioManager,
        context
      );
    } else {
      audioRecorder_ = std::make_shared<AndroidAudioRecorder>(
        sampleRate,
        bufferLength,
        audioEventHandlerRegistry
      );
    }
#else
    if (enableAEC) {
      audioRecorder_ = std::make_shared<IOSAudioRecorderWithAEC>(
        sampleRate,
        bufferLength,
        audioEventHandlerRegistry
      );
    } else {
      audioRecorder_ = std::make_shared<IOSAudioRecorder>(
        sampleRate,
        bufferLength,
        audioEventHandlerRegistry
      );
    }
#endif

    addSetters(JSI_EXPORT_PROPERTY_SETTER(AudioRecorderHostObject, onAudioReady));

    addFunctions(
      JSI_EXPORT_FUNCTION(AudioRecorderHostObject, start),
      JSI_EXPORT_FUNCTION(AudioRecorderHostObject, stop),
      JSI_EXPORT_FUNCTION(AudioRecorderHostObject, connect),
      JSI_EXPORT_FUNCTION(AudioRecorderHostObject, disconnect),
      JSI_EXPORT_FUNCTION(AudioRecorderHostObject, setAECEnabled),
      JSI_EXPORT_FUNCTION(AudioRecorderHostObject, isAECAvailable),
      JSI_EXPORT_FUNCTION(AudioRecorderHostObject, isAECEnabled)
    );
  }

  JSI_HOST_FUNCTION(connect) {
    auto adapterNodeHostObject = args[0].getObject(runtime).getHostObject<RecorderAdapterNodeHostObject>(runtime);
    audioRecorder_->connect(
        std::static_pointer_cast<RecorderAdapterNode>(adapterNodeHostObject->node_));
    return jsi::Value::undefined();
  }

  JSI_HOST_FUNCTION(disconnect) {
    audioRecorder_->disconnect();
    return jsi::Value::undefined();
  }

  JSI_HOST_FUNCTION(start) {
    audioRecorder_->start();

    return jsi::Value::undefined();
  }

  JSI_HOST_FUNCTION(stop) {
    audioRecorder_->stop();

    return jsi::Value::undefined();
  }

  JSI_PROPERTY_SETTER(onAudioReady) {
    audioRecorder_->setOnAudioReadyCallbackId(std::stoull(value.getString(runtime).utf8(runtime)));
  }

  JSI_HOST_FUNCTION(setAECEnabled) {
#ifdef ANDROID
    auto aecRecorder = std::dynamic_pointer_cast<AndroidAudioRecorderWithAEC>(audioRecorder_);
    if (aecRecorder) {
      bool enabled = args[0].getBool();
      aecRecorder->setAECEnabled(enabled);
    }
#else
    auto aecRecorder = std::dynamic_pointer_cast<IOSAudioRecorderWithAEC>(audioRecorder_);
    if (aecRecorder) {
      bool enabled = args[0].getBool();
      aecRecorder->setAECEnabled(enabled);
    }
#endif
    return jsi::Value::undefined();
  }

  JSI_HOST_FUNCTION(isAECAvailable) {
#ifdef ANDROID
    auto aecRecorder = std::dynamic_pointer_cast<AndroidAudioRecorderWithAEC>(audioRecorder_);
    if (aecRecorder) {
      return jsi::Value(aecRecorder->isAECAvailable());
    }
#else
    auto aecRecorder = std::dynamic_pointer_cast<IOSAudioRecorderWithAEC>(audioRecorder_);
    if (aecRecorder) {
      return jsi::Value(aecRecorder->isAECAvailable());
    }
#endif
    return jsi::Value(false);
  }

  JSI_HOST_FUNCTION(isAECEnabled) {
#ifdef ANDROID
    auto aecRecorder = std::dynamic_pointer_cast<AndroidAudioRecorderWithAEC>(audioRecorder_);
    if (aecRecorder) {
      return jsi::Value(aecRecorder->isAECEnabled());
    }
#else
    auto aecRecorder = std::dynamic_pointer_cast<IOSAudioRecorderWithAEC>(audioRecorder_);
    if (aecRecorder) {
      return jsi::Value(aecRecorder->isAECEnabled());
    }
#endif
    return jsi::Value(false);
  }

 private:
  std::shared_ptr<AudioRecorder> audioRecorder_;
};
} // namespace audioapi
