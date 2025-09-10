/**
 * Acoustic Echo Cancellation (AEC) interface for Android audio recording
 * 
 * AEC helps reduce echo and feedback in audio recording by canceling out
 * the audio that is being played through speakers from being recorded
 * by the microphone.
 */

export interface AECAudioRecorderOptions {
  /** Sample rate for audio recording (default: 24000) */
  sampleRate: number;
  /** Buffer length in samples */
  bufferLengthInSamples: number;
  /** Enable Acoustic Echo Cancellation (Android only) */
  enableAEC?: boolean;
}

export interface AECAudioRecorder {
  /** Start audio recording */
  start(): void;
  
  /** Stop audio recording */
  stop(): void;
  
  /** Connect to an audio node */
  connect(node: any): void;
  
  /** Disconnect from audio node */
  disconnect(): void;
  
  /** Set the audio ready callback */
  onAudioReady: string;
  
  /** Enable or disable AEC (Android only) */
  setAECEnabled(enabled: boolean): void;
  
  /** Check if AEC is available on this device (Android only) */
  isAECAvailable(): boolean;
  
  /** Check if AEC is currently enabled (Android only) */
  isAECEnabled(): boolean;
}

/**
 * Example usage:
 * 
 * ```typescript
 * import { createAudioRecorder } from 'react-native-audio-api';
 * 
 * const recorder = createAudioRecorder({
 *   sampleRate: 24000,
 *   bufferLengthInSamples: 1024,
 *   enableAEC: true // Enable AEC on Android
 * });
 * 
 * // Check if AEC is available
 * if (recorder.isAECAvailable()) {
 *   console.log('AEC is available on this device');
 *   
 *   // Enable AEC
 *   recorder.setAECEnabled(true);
 *   
 *   // Check if AEC is enabled
 *   console.log('AEC enabled:', recorder.isAECEnabled());
 * } else {
 *   console.log('AEC is not available on this device');
 * }
 * 
 * // Set up audio processing
 * recorder.onAudioReady = 'audioReadyCallback';
 * 
 * // Start recording
 * recorder.start();
 * ```
 */
