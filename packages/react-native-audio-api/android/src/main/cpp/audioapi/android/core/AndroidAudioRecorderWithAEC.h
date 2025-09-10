#pragma once

#include <audioapi/core/inputs/AudioRecorder.h>
#include <jni.h>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace audioapi {

class AudioBus;

class AndroidAudioRecorderWithAEC : public AudioRecorder {
 public:
    AndroidAudioRecorderWithAEC(
        float sampleRate,
        int bufferLength,
        const std::shared_ptr<AudioEventHandlerRegistry> &audioEventHandlerRegistry,
        jobject audioManager,
        jobject context);

    ~AndroidAudioRecorderWithAEC() override;

    void start() override;
    void stop() override;

    // AEC control methods
    void setAECEnabled(bool enabled);
    bool isAECAvailable() const;
    bool isAECEnabled() const;

 private:
    void recordingThread();
    void initializeAudioRecord();
    void cleanupAudioRecord();
    void cleanupAEC();

    jobject audioManager_;
    jobject context_;
    jobject audioRecord_;
    jobject acousticEchoCanceler_;
    jobject noiseSuppressor_;
    
    std::thread recordingThread_;
    std::atomic<bool> shouldStop_;
    std::atomic<bool> aecEnabled_;
    std::atomic<bool> aecAvailable_;
    
    mutable std::mutex audioRecordMutex_;
    
    // JNI method IDs (cached for performance)
    jmethodID audioRecordConstructor_;
    jmethodID audioRecordStartRecording_;
    jmethodID audioRecordStop_;
    jmethodID audioRecordRead_;
    jmethodID audioRecordRelease_;
    jmethodID audioRecordGetAudioSessionId_;
    jmethodID audioRecordGetRecordingState_;
    jmethodID audioRecordGetState_;
    
    jmethodID acousticEchoCancelerIsAvailable_;
    jmethodID acousticEchoCancelerCreate_;
    jmethodID acousticEchoCancelerSetEnabled_;
    jmethodID acousticEchoCancelerRelease_;
    
    jmethodID noiseSuppressorIsAvailable_;
    jmethodID noiseSuppressorCreate_;
    jmethodID noiseSuppressorSetEnabled_;
    jmethodID noiseSuppressorRelease_;
    
    jmethodID audioManagerGetProperty_;
    jmethodID audioManagerGenerateAudioSessionId_;
    
    // Constants
    static constexpr int SAMPLE_RATE = 24000;
    static constexpr int AUDIO_FORMAT = 2; // AudioFormat.ENCODING_PCM_16BIT
    static constexpr int CHANNEL_CONFIG = 16; // AudioFormat.CHANNEL_IN_MONO
    static constexpr int AUDIO_SOURCE = 7; // MediaRecorder.AudioSource.VOICE_COMMUNICATION
};

} // namespace audioapi
