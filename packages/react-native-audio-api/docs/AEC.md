# Acoustic Echo Cancellation (AEC) Support

This library now supports Acoustic Echo Cancellation (AEC) on Android devices, which helps reduce echo and feedback in audio recording by canceling out the audio that is being played through speakers from being recorded by the microphone.

## Features

- **Native Android AEC**: Uses Android's built-in `AcousticEchoCanceler` API
- **Automatic Noise Suppression**: Enables noise suppression when available
- **Runtime Control**: Enable/disable AEC during recording
- **Availability Detection**: Check if AEC is supported on the device
- **Seamless Integration**: Works with existing audio recording pipeline

## Requirements

- Android API level 16+ (Android 4.1+)
- Device must support AEC (most modern Android devices do)
- Audio recording permissions

## Usage

### Basic Setup

```typescript
import { createAudioRecorder } from 'react-native-audio-api';

const recorder = createAudioRecorder({
  sampleRate: 24000,
  bufferLengthInSamples: 1024,
  enableAEC: true // Enable AEC on Android
});
```

### Check AEC Availability

```typescript
if (recorder.isAECAvailable()) {
  console.log('AEC is available on this device');
} else {
  console.log('AEC is not available on this device');
}
```

### Control AEC

```typescript
// Enable AEC
recorder.setAECEnabled(true);

// Disable AEC
recorder.setAECEnabled(false);

// Check if AEC is enabled
const isEnabled = recorder.isAECEnabled();
console.log('AEC enabled:', isEnabled);
```

### Complete Example

```typescript
import React, { useEffect, useState } from 'react';
import { createAudioRecorder, createAudioContext } from 'react-native-audio-api';

const AudioRecorderWithAEC = () => {
  const [recorder, setRecorder] = useState(null);
  const [isRecording, setIsRecording] = useState(false);

  useEffect(() => {
    // Create audio context
    const context = createAudioContext(24000, false);

    // Create recorder with AEC
    const audioRecorder = createAudioRecorder({
      sampleRate: 24000,
      bufferLengthInSamples: 1024,
      enableAEC: true
    });

    setRecorder(audioRecorder);

    // Check AEC availability
    if (audioRecorder.isAECAvailable()) {
      console.log('AEC is available');
      audioRecorder.setAECEnabled(true);
    }

    // Set up audio processing
    audioRecorder.onAudioReady = 'processAudioData';

    return () => {
      audioRecorder.stop();
      audioRecorder.disconnect();
    };
  }, []);

  const processAudioData = (event) => {
    // Process audio data with AEC applied
    console.log('Audio data:', event.buffer);
  };

  const toggleRecording = () => {
    if (isRecording) {
      recorder.stop();
    } else {
      recorder.start();
    }
    setIsRecording(!isRecording);
  };

  return (
    <div>
      <button onClick={toggleRecording}>
        {isRecording ? 'Stop' : 'Start'} Recording
      </button>
      <p>AEC Available: {recorder?.isAECAvailable() ? 'Yes' : 'No'}</p>
      <p>AEC Enabled: {recorder?.isAECEnabled() ? 'Yes' : 'No'}</p>
    </div>
  );
};
```

## API Reference

### AudioRecorderOptions

```typescript
interface AECAudioRecorderOptions {
  sampleRate: number;           // Sample rate for recording
  bufferLengthInSamples: number; // Buffer length in samples
  enableAEC?: boolean;          // Enable AEC (Android only)
}
```

### AECAudioRecorder Methods

| Method | Description | Platform |
|--------|-------------|----------|
| `start()` | Start audio recording | All |
| `stop()` | Stop audio recording | All |
| `connect(node)` | Connect to audio node | All |
| `disconnect()` | Disconnect from audio node | All |
| `setAECEnabled(enabled)` | Enable/disable AEC | Android, iOS |
| `isAECAvailable()` | Check AEC availability | Android, iOS |
| `isAECEnabled()` | Check if AEC is enabled | Android, iOS |

## Implementation Details

### Android Implementation

The AEC implementation uses:

- **AudioRecord API**: Direct access to Android's audio recording
- **AcousticEchoCanceler**: Android's native AEC implementation
- **NoiseSuppressor**: Automatic noise suppression when available
- **VOICE_COMMUNICATION**: Optimized audio source for voice applications

### iOS Implementation

The AEC implementation uses:

- **AVAudioEngine**: iOS's high-level audio processing framework
- **Voice Processing**: Built-in voice processing capabilities including AEC
- **AVAudioSession**: Optimized for voice communication mode
- **Real-time Processing**: Integrated with existing audio graph

### Audio Processing Pipeline

1. **Audio Capture**: Raw audio from microphone via AudioRecord
2. **AEC Processing**: Echo cancellation applied by Android
3. **Noise Suppression**: Additional noise reduction (if available)
4. **Format Conversion**: 16-bit PCM to float conversion
5. **Buffer Management**: Integration with existing audio pipeline

### Performance Considerations

- **Low Latency**: Direct AudioRecord usage minimizes processing delay
- **CPU Efficient**: Native Android processing reduces CPU usage
- **Memory Optimized**: Efficient buffer management
- **Thread Safe**: Proper synchronization for multi-threaded access

## Troubleshooting

### AEC Not Available

If `isAECAvailable()` returns `false`:

1. Check Android version (requires API 16+)
2. Verify device supports AEC (most modern devices do)
3. Ensure proper audio permissions are granted

### AEC Not Working

If AEC is available but not working:

1. Check if AEC is enabled: `recorder.isAECEnabled()`
2. Verify audio session is properly initialized
3. Ensure recording is started before enabling AEC
4. Check device audio configuration

### Performance Issues

If experiencing performance issues:

1. Reduce buffer size for lower latency
2. Check if noise suppression is causing overhead
3. Monitor CPU usage during recording
4. Consider disabling AEC if not needed

## Platform Support

| Platform | AEC Support | Notes |
|----------|-------------|-------|
| Android | ✅ Full Support | Native AcousticEchoCanceler API |
| iOS | ✅ Full Support | AVAudioEngine voice processing (iOS 10.0+) |
| Web | ❌ Not Supported | Browser-dependent, not implemented |

## License

This AEC implementation is part of the react-native-audio-api library and follows the same LGPL v3 license.
