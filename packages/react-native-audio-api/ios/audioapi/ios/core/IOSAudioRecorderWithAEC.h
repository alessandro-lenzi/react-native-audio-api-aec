#pragma once

#ifdef __OBJC__ // when compiled as Objective-C++
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#else // when compiled as C++
typedef struct objc_object AVAudioEngine;
typedef struct objc_object AVAudioFormat;
typedef struct objc_object AVAudioInputNode;
typedef struct objc_object AVAudioPlayerNode;
typedef struct objc_object AVAudioMixerNode;
typedef struct objc_object AVAudioPCMBuffer;
typedef struct objc_object NSObject;
#endif // __OBJC__

#include <audioapi/core/inputs/AudioRecorder.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

namespace audioapi {

class AudioBus;
class CircularAudioArray;

class IOSAudioRecorderWithAEC : public AudioRecorder {
 public:
  IOSAudioRecorderWithAEC(
      float sampleRate,
      int bufferLength,
      const std::shared_ptr<AudioEventHandlerRegistry>
          &audioEventHandlerRegistry);

  ~IOSAudioRecorderWithAEC() override;

  void start() override;
  void stop() override;

  // AEC control methods
  void setAECEnabled(bool enabled);
  bool isAECAvailable() const;
  bool isAECEnabled() const;
  void setVoiceProcessingBypassed(bool bypassed);
  void setInputMuted(bool muted);

 private:
  void setupAudioSession();
  void setupAudioEngine();
  void cleanupAudioEngine();
  void processMicrophoneBuffer(AVAudioPCMBuffer* buffer, AVAudioTime* when);
  void processOutputBuffer(AVAudioPCMBuffer* buffer, AVAudioTime* when);
  void updateInputVolume();
  void updateOutputVolume();
  float calculateRMSLevel(const float* buffer, int frameCount);

  // Audio engine components
  AVAudioEngine* audioEngine_;
  AVAudioInputNode* inputNode_;
  AVAudioPlayerNode* playerNode_;
  AVAudioMixerNode* mainMixerNode_;
  AVAudioFormat* voiceIOFormat_;
  
  // AEC state
  std::atomic<bool> aecEnabled_;
  std::atomic<bool> voiceProcessingBypassed_;
  std::atomic<bool> inputMuted_;
  std::atomic<bool> isRecording_;
  
  // Audio processing
  std::vector<float> inputBuffer_;
  std::vector<float> outputBuffer_;
  std::atomic<int> inputBufferIndex_;
  std::atomic<int> outputBufferIndex_;
  std::mutex bufferMutex_;
  
  // AEC adaptation timing
  std::atomic<bool> hasFirstInputBeenDiscarded_;
  std::atomic<bool> discardRecording_;
  int discardFirstInputMillis_;
  
  // Threading
  std::thread audioProcessingThread_;
  std::atomic<bool> shouldStop_;
  
  // Callbacks
  void* onMicDataCallback_;
  void* onInputVolumeCallback_;
  void* onOutputVolumeCallback_;
  void* onAudioInterruptionCallback_;
};

} // namespace audioapi
