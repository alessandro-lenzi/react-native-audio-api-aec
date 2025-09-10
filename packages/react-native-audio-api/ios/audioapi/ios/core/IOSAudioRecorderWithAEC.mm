#import <AVFoundation/AVFoundation.h>
#import <Accelerate/Accelerate.h>
#import <os/lock.h>

#include <audioapi/core/Constants.h>
#include <audioapi/dsp/VectorMath.h>
#include <audioapi/events/AudioEventHandlerRegistry.h>
#include <audioapi/ios/core/IOSAudioRecorderWithAEC.h>
#include <audioapi/utils/AudioArray.h>
#include <audioapi/utils/AudioBus.h>
#include <audioapi/utils/CircularAudioArray.h>
#include <audioapi/utils/CircularOverflowableAudioArray.h>
#include <unordered_map>
#include <chrono>

namespace audioapi {

IOSAudioRecorderWithAEC::IOSAudioRecorderWithAEC(
    float sampleRate,
    int bufferLength,
    const std::shared_ptr<AudioEventHandlerRegistry> &audioEventHandlerRegistry)
    : AudioRecorder(sampleRate, bufferLength, audioEventHandlerRegistry),
      audioEngine_(nullptr),
      inputNode_(nullptr),
      playerNode_(nullptr),
      mainMixerNode_(nullptr),
      voiceIOFormat_(nullptr),
      aecEnabled_(false),
      voiceProcessingBypassed_(false),
      inputMuted_(false),
      isRecording_(false),
      inputBuffer_(2048, 0.0f),
      outputBuffer_(2048, 0.0f),
      inputBufferIndex_(0),
      outputBufferIndex_(0),
      hasFirstInputBeenDiscarded_(false),
      discardRecording_(false),
      discardFirstInputMillis_(2000),
      shouldStop_(false),
      onMicDataCallback_(nullptr),
      onInputVolumeCallback_(nullptr),
      onOutputVolumeCallback_(nullptr),
      onAudioInterruptionCallback_(nullptr) {
  
  setupAudioSession();
  setupAudioEngine();
}

IOSAudioRecorderWithAEC::~IOSAudioRecorderWithAEC() {
  stop();
  cleanupAudioEngine();
}

void IOSAudioRecorderWithAEC::start() {
  if (isRunning_.load()) {
    return;
  }

  if (audioEngine_ && ![audioEngine_ isRunning]) {
    NSError* error = nil;
    [audioEngine_ startAndReturnError:&error];
    if (error) {
      NSLog(@"Error starting audio engine: %@", [error localizedDescription]);
      return;
    }
  }

  isRecording_.store(true);
  inputMuted_.store(false);
  isRunning_.store(true);
}

void IOSAudioRecorderWithAEC::stop() {
  if (!isRunning_.load()) {
    return;
  }

  isRecording_.store(false);
  inputMuted_.store(true);
  isRunning_.store(false);

  if (audioEngine_ && [audioEngine_ isRunning]) {
    [audioEngine_ stop];
  }

  sendRemainingData();
}

void IOSAudioRecorderWithAEC::setAECEnabled(bool enabled) {
  aecEnabled_.store(enabled);
  
  if (inputNode_) {
    NSError* error = nil;
    BOOL success = [inputNode_ setVoiceProcessingEnabled:enabled error:&error];
    if (!success && error) {
      NSLog(@"Error setting voice processing enabled: %@", [error localizedDescription]);
    } else {
      NSLog(@"Voice processing %@", enabled ? "enabled" : "disabled");
    }
  }
}

bool IOSAudioRecorderWithAEC::isAECAvailable() const {
  // AEC is available on iOS 10.0+ through AVAudioEngine voice processing
  if (@available(iOS 10.0, *)) {
    return true;
  }
  return false;
}

bool IOSAudioRecorderWithAEC::isAECEnabled() const {
  return aecEnabled_.load();
}

void IOSAudioRecorderWithAEC::setVoiceProcessingBypassed(bool bypassed) {
  voiceProcessingBypassed_.store(bypassed);
  
  if (inputNode_) {
    inputNode_.isVoiceProcessingBypassed = bypassed;
  }
}

void IOSAudioRecorderWithAEC::setInputMuted(bool muted) {
  inputMuted_.store(muted);
  
  if (inputNode_) {
    inputNode_.isVoiceProcessingInputMuted = muted;
  }
}

void IOSAudioRecorderWithAEC::setupAudioSession() {
  AVAudioSession* session = [AVAudioSession sharedInstance];
  
  NSError* error = nil;
  
  // Set category for voice communication with AEC
  BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                 mode:AVAudioSessionModeVoiceChat
                              options:AVAudioSessionCategoryOptionDefaultToSpeaker |
                                      AVAudioSessionCategoryOptionAllowBluetooth |
                                      AVAudioSessionCategoryOptionAllowBluetoothA2DP
                                error:&error];
  
  if (!success) {
    NSLog(@"Could not set audio session category: %@", [error localizedDescription]);
  }
  
  // Set preferred sample rate
  success = [session setPreferredSampleRate:sampleRate_ error:&error];
  if (!success) {
    NSLog(@"Could not set preferred sample rate: %@", [error localizedDescription]);
  }
  
  // Set preferred IO buffer duration for optimal AEC performance
  // 1024 samples @ 24kHz ≈ 42.7 ms
  double bufferDuration = 1024.0 / sampleRate_;
  success = [session setPreferredIOBufferDuration:bufferDuration error:&error];
  if (!success) {
    NSLog(@"Could not set IO buffer duration: %@", [error localizedDescription]);
  }
  
  // Activate the session
  success = [session setActive:YES error:&error];
  if (!success) {
    NSLog(@"Could not activate audio session: %@", [error localizedDescription]);
  }
}

void IOSAudioRecorderWithAEC::setupAudioEngine() {
  audioEngine_ = [[AVAudioEngine alloc] init];
  inputNode_ = [audioEngine_ inputNode];
  playerNode_ = [[AVAudioPlayerNode alloc] init];
  mainMixerNode_ = [audioEngine_ mainMixerNode];
  
  // Create voice IO format
  voiceIOFormat_ = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                    sampleRate:sampleRate_
                                                      channels:1
                                                   interleaved:NO];
  
  // Attach nodes
  [audioEngine_ attachNode:playerNode_];
  
  // Connect nodes
  [audioEngine_ connect:playerNode_ to:mainMixerNode_ format:voiceIOFormat_];
  [audioEngine_ connect:mainMixerNode_ to:[audioEngine_ outputNode] format:voiceIOFormat_];
  
  // Enable voice processing on input node
  if (@available(iOS 10.0, *)) {
    NSError* error = nil;
    BOOL success = [inputNode_ setVoiceProcessingEnabled:YES error:&error];
    if (success) {
      aecEnabled_.store(true);
      NSLog(@"Voice processing enabled successfully");
    } else {
      NSLog(@"Could not enable voice processing: %@", [error localizedDescription]);
    }
  }
  
  // Set initial input muting state
  inputNode_.isVoiceProcessingInputMuted = !isRecording_.load();
  
  // Install tap on input node for microphone processing
  [inputNode_ installTapOnBus:0
                   bufferSize:1024
                       format:voiceIOFormat_
                        block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
      [self processMicrophoneBuffer:buffer when:when];
    }];
  
  // Install tap on main mixer for output processing
  [mainMixerNode_ installTapOnBus:0
                       bufferSize:1024
                           format:voiceIOFormat_
                            block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
      [self processOutputBuffer:buffer when:when];
    }];
  
  // Prepare the engine
  [audioEngine_ prepare];
}

void IOSAudioRecorderWithAEC::cleanupAudioEngine() {
  if (audioEngine_) {
    [audioEngine_ stop];
    [audioEngine_ reset];
    audioEngine_ = nil;
  }
  
  inputNode_ = nil;
  playerNode_ = nil;
  mainMixerNode_ = nil;
  voiceIOFormat_ = nil;
}

void IOSAudioRecorderWithAEC::processMicrophoneBuffer(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
  if (!isRecording_.load() || discardRecording_.load()) {
    return;
  }
  
  float* channelData = (float*)buffer.floatChannelData[0];
  int frameCount = (int)buffer.frameLength;
  
  // Update input buffer for volume calculation
  std::lock_guard<std::mutex> lock(bufferMutex_);
  for (int i = 0; i < frameCount; i++) {
    inputBuffer_[inputBufferIndex_.load()] = channelData[i];
    inputBufferIndex_.store((inputBufferIndex_.load() + 1) % inputBuffer_.size());
  }
  
  // Process audio data through existing pipeline
  writeToBuffers(channelData, frameCount);
  
  // Process through audio graph
  while (circularBuffer_->getNumberOfAvailableFrames() >= bufferLength_) {
    auto bus = std::make_shared<AudioBus>(bufferLength_, 1, sampleRate_);
    auto* outputChannel = bus->getChannel(0)->getData();
    
    circularBuffer_->pop_front(outputChannel, bufferLength_);
    
    // Convert AVAudioTime to double timestamp
    double whenTime = when.sampleTime / when.sampleRate;
    invokeOnAudioReadyCallback(bus, bufferLength_, whenTime);
  }
  
  // Update input volume
  updateInputVolume();
}

void IOSAudioRecorderWithAEC::processOutputBuffer(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
  float* channelData = (float*)buffer.floatChannelData[0];
  int frameCount = (int)buffer.frameLength;
  
  // Update output buffer for volume calculation and FFT
  std::lock_guard<std::mutex> lock(bufferMutex_);
  for (int i = 0; i < frameCount; i++) {
    outputBuffer_[outputBufferIndex_.load()] = channelData[i];
    outputBufferIndex_.store((outputBufferIndex_.load() + 1) % outputBuffer_.size());
  }
  
  // Update output volume
  updateOutputVolume();
}

void IOSAudioRecorderWithAEC::updateInputVolume() {
  std::lock_guard<std::mutex> lock(bufferMutex_);
  float volume = calculateRMSLevel(inputBuffer_.data(), inputBuffer_.size());
  
  // Call input volume callback if set
  if (onInputVolumeCallback_) {
    // This would need to be implemented with proper callback mechanism
    // For now, we'll just log it
    NSLog(@"Input volume: %f", volume);
  }
}

void IOSAudioRecorderWithAEC::updateOutputVolume() {
  std::lock_guard<std::mutex> lock(bufferMutex_);
  float volume = calculateRMSLevel(outputBuffer_.data(), outputBuffer_.size());
  
  // Call output volume callback if set
  if (onOutputVolumeCallback_) {
    // This would need to be implemented with proper callback mechanism
    // For now, we'll just log it
    NSLog(@"Output volume: %f", volume);
  }
}

float IOSAudioRecorderWithAEC::calculateRMSLevel(const float* buffer, int frameCount) {
  const float epsilon = 1e-5f; // To avoid log(0)
  
  float meanSquare = 0.0f;
  vDSP_measqv(buffer, 1, &meanSquare, vDSP_Length(frameCount));
  
  float rmsValue = sqrtf(meanSquare);
  float dbValue = 20.0f * log10f(fmaxf(rmsValue, epsilon));
  
  const float minDb = -80.0f;
  float normalizedValue = fmaxf(0.0f, fminf(1.0f, (dbValue - minDb) / fabsf(minDb)));
  
  const float expFactor = 2.0f;
  return powf(normalizedValue, expFactor);
}

} // namespace audioapi
