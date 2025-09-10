#include <audioapi/android/core/AndroidAudioRecorderWithAEC.h>
#include <audioapi/core/Constants.h>
#include <audioapi/core/sources/RecorderAdapterNode.h>
#include <audioapi/events/AudioEventHandlerRegistry.h>
#include <audioapi/utils/AudioArray.h>
#include <audioapi/utils/AudioBus.h>
#include <audioapi/utils/CircularAudioArray.h>
#include <audioapi/utils/CircularOverflowableAudioArray.h>
#include <android/log.h>
#include <jni.h>

#define LOG_TAG "AndroidAudioRecorderWithAEC"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace audioapi {

AndroidAudioRecorderWithAEC::AndroidAudioRecorderWithAEC(
    float sampleRate,
    int bufferLength,
    const std::shared_ptr<AudioEventHandlerRegistry> &audioEventHandlerRegistry,
    jobject audioManager,
    jobject context)
    : AudioRecorder(sampleRate, bufferLength, audioEventHandlerRegistry),
      audioManager_(audioManager),
      context_(context),
      audioRecord_(nullptr),
      acousticEchoCanceler_(nullptr),
      noiseSuppressor_(nullptr),
      shouldStop_(false),
      aecEnabled_(false),
      aecAvailable_(false) {
  
  // Cache JNI method IDs
  JNIEnv* env = nullptr;
  JavaVM* vm = nullptr;
  
  if (audioManager_ != nullptr) {
    // Get JNI environment
    JavaVM* vm = nullptr;
    jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
    if (result != JNI_OK || vm == nullptr) {
      LOGE("Failed to get JavaVM");
      return;
    }
    
    result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (result != JNI_OK || env == nullptr) {
      LOGE("Failed to get JNI environment");
      return;
    }
    
    // Cache AudioRecord method IDs
    jclass audioRecordClass = env->FindClass("android/media/AudioRecord");
    audioRecordConstructor_ = env->GetMethodID(audioRecordClass, "<init>", "(IIIII)V");
    audioRecordStartRecording_ = env->GetMethodID(audioRecordClass, "startRecording", "()V");
    audioRecordStop_ = env->GetMethodID(audioRecordClass, "stop", "()V");
    audioRecordRead_ = env->GetMethodID(audioRecordClass, "read", "([BII)I");
    audioRecordRelease_ = env->GetMethodID(audioRecordClass, "release", "()V");
    audioRecordGetAudioSessionId_ = env->GetMethodID(audioRecordClass, "getAudioSessionId", "()I");
    audioRecordGetRecordingState_ = env->GetMethodID(audioRecordClass, "getRecordingState", "()I");
    audioRecordGetState_ = env->GetMethodID(audioRecordClass, "getState", "()I");
    
    // Cache AcousticEchoCanceler method IDs
    jclass aecClass = env->FindClass("android/media/audiofx/AcousticEchoCanceler");
    acousticEchoCancelerIsAvailable_ = env->GetStaticMethodID(aecClass, "isAvailable", "()Z");
    acousticEchoCancelerCreate_ = env->GetStaticMethodID(aecClass, "create", "(I)Landroid/media/audiofx/AcousticEchoCanceler;");
    acousticEchoCancelerSetEnabled_ = env->GetMethodID(aecClass, "setEnabled", "(Z)I");
    acousticEchoCancelerRelease_ = env->GetMethodID(aecClass, "release", "()V");
    
    // Cache NoiseSuppressor method IDs
    jclass nsClass = env->FindClass("android/media/audiofx/NoiseSuppressor");
    noiseSuppressorIsAvailable_ = env->GetStaticMethodID(nsClass, "isAvailable", "()Z");
    noiseSuppressorCreate_ = env->GetStaticMethodID(nsClass, "create", "(I)Landroid/media/audiofx/NoiseSuppressor;");
    noiseSuppressorSetEnabled_ = env->GetMethodID(nsClass, "setEnabled", "(Z)I");
    noiseSuppressorRelease_ = env->GetMethodID(nsClass, "release", "()V");
    
    // Cache AudioManager method IDs
    jclass audioManagerClass = env->FindClass("android/media/AudioManager");
    audioManagerGetProperty_ = env->GetMethodID(audioManagerClass, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
    audioManagerGenerateAudioSessionId_ = env->GetMethodID(audioManagerClass, "generateAudioSessionId", "()I");
    
    // Check AEC availability
    aecAvailable_ = env->CallStaticBooleanMethod(aecClass, acousticEchoCancelerIsAvailable_);
    LOGI("AEC available: %s", aecAvailable_ ? "true" : "false");
  }
}

AndroidAudioRecorderWithAEC::~AndroidAudioRecorderWithAEC() {
  stop();
  cleanupAudioRecord();
  cleanupAEC();
}

void AndroidAudioRecorderWithAEC::start() {
  if (isRunning_.load()) {
    return;
  }

  std::lock_guard<std::mutex> lock(audioRecordMutex_);
  
  initializeAudioRecord();
  
  if (audioRecord_ != nullptr) {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    
    jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
    if (result == JNI_OK && vm != nullptr) {
      result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
      if (result == JNI_OK && env != nullptr) {
        env->CallVoidMethod(audioRecord_, audioRecordStartRecording_);
        
        // Check if recording started successfully
        jint state = env->CallIntMethod(audioRecord_, audioRecordGetState_);
        if (state == 1) { // AudioRecord.STATE_INITIALIZED
          jint recordingState = env->CallIntMethod(audioRecord_, audioRecordGetRecordingState_);
          if (recordingState == 3) { // AudioRecord.RECORDSTATE_RECORDING
            shouldStop_ = false;
            recordingThread_ = std::thread(&AndroidAudioRecorderWithAEC::recordingThread, this);
            isRunning_.store(true);
            LOGI("Audio recording started successfully");
          } else {
            LOGE("Failed to start recording - invalid recording state: %d", recordingState);
          }
        } else {
          LOGE("Failed to start recording - invalid state: %d", state);
        }
      }
    }
  }
}

void AndroidAudioRecorderWithAEC::stop() {
  if (!isRunning_.load()) {
    return;
  }

  shouldStop_ = true;
  isRunning_.store(false);

  if (recordingThread_.joinable()) {
    recordingThread_.join();
  }

  std::lock_guard<std::mutex> lock(audioRecordMutex_);
  if (audioRecord_ != nullptr) {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    
    jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
    if (result == JNI_OK && vm != nullptr) {
      result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
      if (result == JNI_OK && env != nullptr) {
        env->CallVoidMethod(audioRecord_, audioRecordStop_);
        LOGI("Audio recording stopped");
      }
    }
  }

  sendRemainingData();
}

void AndroidAudioRecorderWithAEC::setAECEnabled(bool enabled) {
  if (!aecAvailable_) {
    LOGE("AEC is not available on this device");
    return;
  }

  aecEnabled_ = enabled;
  
  if (acousticEchoCanceler_ != nullptr) {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    
    jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
    if (result == JNI_OK && vm != nullptr) {
      result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
      if (result == JNI_OK && env != nullptr) {
        jint setResult = env->CallIntMethod(acousticEchoCanceler_, acousticEchoCancelerSetEnabled_, enabled);
        if (setResult == 0) { // AudioEffect.SUCCESS
          LOGI("AEC %s", enabled ? "enabled" : "disabled");
        } else {
          LOGE("Failed to %s AEC, result: %d", enabled ? "enable" : "disable", setResult);
        }
      }
    }
  }
}

bool AndroidAudioRecorderWithAEC::isAECAvailable() const {
  return aecAvailable_;
}

bool AndroidAudioRecorderWithAEC::isAECEnabled() const {
  return aecEnabled_;
}

void AndroidAudioRecorderWithAEC::recordingThread() {
  JNIEnv* env = nullptr;
  JavaVM* vm = nullptr;
  
  jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
  if (result != JNI_OK || vm == nullptr) {
    LOGE("Failed to get JavaVM in recording thread");
    return;
  }
  
  result = vm->AttachCurrentThread(&env, nullptr);
  if (result != JNI_OK || env == nullptr) {
    LOGE("Failed to attach thread to JVM");
    return;
  }

  const int bufferSize = 1024;
  jbyteArray audioBuffer = env->NewByteArray(bufferSize);
  jbyte* audioBufferPtr = env->GetByteArrayElements(audioBuffer, nullptr);

  while (!shouldStop_.load()) {
    std::lock_guard<std::mutex> lock(audioRecordMutex_);
    
    if (audioRecord_ != nullptr) {
      jint bytesRead = env->CallIntMethod(audioRecord_, audioRecordRead_, audioBuffer, 0, bufferSize);
      
      if (bytesRead > 0) {
        // Convert 16-bit PCM to float
        const int numSamples = bytesRead / 2;
        std::vector<float> floatSamples(numSamples);
        
        for (int i = 0; i < numSamples; i++) {
          int16_t sample = static_cast<int16_t>((audioBufferPtr[i * 2] & 0xFF) | 
                                               ((audioBufferPtr[i * 2 + 1] & 0xFF) << 8));
          floatSamples[i] = sample / 32768.0f;
        }
        
        writeToBuffers(floatSamples.data(), numSamples);
        
        // Process audio data through the existing pipeline
        while (circularBuffer_->getNumberOfAvailableFrames() >= bufferLength_) {
          auto bus = std::make_shared<AudioBus>(bufferLength_, 1, sampleRate_);
          auto *outputChannel = bus->getChannel(0)->getData();
          
          circularBuffer_->pop_front(outputChannel, bufferLength_);
          auto when = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::high_resolution_clock::now().time_since_epoch()).count()) / 1e9;
          
          invokeOnAudioReadyCallback(bus, bufferLength_, when);
        }
      } else if (bytesRead < 0) {
        LOGE("Error reading audio data: %d", bytesRead);
        break;
      }
    }
  }

  env->ReleaseByteArrayElements(audioBuffer, audioBufferPtr, JNI_ABORT);
  env->DeleteLocalRef(audioBuffer);
  
  vm->DetachCurrentThread();
}

void AndroidAudioRecorderWithAEC::initializeAudioRecord() {
  if (audioRecord_ != nullptr) {
    return; // Already initialized
  }

  JNIEnv* env = nullptr;
  JavaVM* vm = nullptr;
  
  jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
  if (result != JNI_OK || vm == nullptr) {
    LOGE("Failed to get JavaVM");
    return;
  }
  
  result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
  if (result != JNI_OK || env == nullptr) {
    LOGE("Failed to get JNI environment");
    return;
  }

  // Generate audio session ID
  jint audioSessionId = env->CallIntMethod(audioManager_, audioManagerGenerateAudioSessionId_);
  
  // Calculate buffer size
  jint bufferSize = 1024; // Default buffer size
  
  // Create AudioRecord
  jclass audioRecordClass = env->FindClass("android/media/AudioRecord");
  jobject audioRecord = env->NewObject(audioRecordClass, audioRecordConstructor_,
                                      AUDIO_SOURCE, SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT, bufferSize);
  
  if (audioRecord != nullptr) {
    audioRecord_ = env->NewGlobalRef(audioRecord);
    env->DeleteLocalRef(audioRecord);
    
    // Initialize AEC if available
    if (aecAvailable_) {
      jclass aecClass = env->FindClass("android/media/audiofx/AcousticEchoCanceler");
      jobject aec = env->CallStaticObjectMethod(aecClass, acousticEchoCancelerCreate_, audioSessionId);
      
      if (aec != nullptr) {
        acousticEchoCanceler_ = env->NewGlobalRef(aec);
        env->DeleteLocalRef(aec);
        
        // Enable AEC by default
        setAECEnabled(true);
      }
    }
    
    // Initialize Noise Suppressor if available
    jclass nsClass = env->FindClass("android/media/audiofx/NoiseSuppressor");
    jboolean nsAvailable = env->CallStaticBooleanMethod(nsClass, noiseSuppressorIsAvailable_);
    
    if (nsAvailable) {
      jobject ns = env->CallStaticObjectMethod(nsClass, noiseSuppressorCreate_, audioSessionId);
      
      if (ns != nullptr) {
        noiseSuppressor_ = env->NewGlobalRef(ns);
        env->DeleteLocalRef(ns);
        
        // Enable noise suppressor
        env->CallIntMethod(noiseSuppressor_, noiseSuppressorSetEnabled_, true);
        LOGI("Noise Suppressor enabled");
      }
    }
    
    LOGI("AudioRecord initialized with session ID: %d", audioSessionId);
  } else {
    LOGE("Failed to create AudioRecord");
  }
}

void AndroidAudioRecorderWithAEC::cleanupAudioRecord() {
  if (audioRecord_ != nullptr) {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    
    jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
    if (result == JNI_OK && vm != nullptr) {
      result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
      if (result == JNI_OK && env != nullptr) {
        env->CallVoidMethod(audioRecord_, audioRecordRelease_);
        env->DeleteGlobalRef(audioRecord_);
        audioRecord_ = nullptr;
      }
    }
  }
}

void AndroidAudioRecorderWithAEC::cleanupAEC() {
  JNIEnv* env = nullptr;
  JavaVM* vm = nullptr;
  
  jint result = JNI_GetCreatedJavaVMs(&vm, 1, nullptr);
  if (result == JNI_OK && vm != nullptr) {
    result = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (result == JNI_OK && env != nullptr) {
      if (acousticEchoCanceler_ != nullptr) {
        env->CallVoidMethod(acousticEchoCanceler_, acousticEchoCancelerRelease_);
        env->DeleteGlobalRef(acousticEchoCanceler_);
        acousticEchoCanceler_ = nullptr;
      }
      
      if (noiseSuppressor_ != nullptr) {
        env->CallVoidMethod(noiseSuppressor_, noiseSuppressorRelease_);
        env->DeleteGlobalRef(noiseSuppressor_);
        noiseSuppressor_ = nullptr;
      }
    }
  }
}

} // namespace audioapi
