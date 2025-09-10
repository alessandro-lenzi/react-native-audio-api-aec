package com.swmansion.audioapi

import android.content.Context
import android.media.AudioManager
import com.facebook.jni.HybridData
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableArray
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.common.annotations.FrameworkAPI
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.turbomodule.core.CallInvokerHolderImpl
import com.swmansion.audioapi.system.MediaSessionManager
import com.swmansion.audioapi.system.PermissionRequestListener
import java.lang.ref.WeakReference

@OptIn(FrameworkAPI::class)
@ReactModule(name = AudioAPIModule.NAME)
class AudioAPIModule(
  reactContext: ReactApplicationContext,
) : NativeAudioAPIModuleSpec(reactContext) {
  companion object {
    const val NAME = NativeAudioAPIModuleSpec.NAME
  }

  val reactContext: WeakReference<ReactApplicationContext> = WeakReference(reactContext)

  private val mHybridData: HybridData

  external fun initHybrid(
    jsContext: Long,
    callInvoker: CallInvokerHolderImpl,
  ): HybridData

  private external fun injectJSIBindings()
  
  private external fun injectJSIBindingsWithContext(context: Context, audioManager: AudioManager)

  external fun invokeHandlerWithEventNameAndEventBody(
    eventName: String,
    eventBody: Map<String, Any>,
  )

  init {
    try {
      System.loadLibrary("react-native-audio-api")
      val jsCallInvokerHolder = reactContext.jsCallInvokerHolder as CallInvokerHolderImpl
      mHybridData = initHybrid(reactContext.javaScriptContextHolder!!.get(), jsCallInvokerHolder)
    } catch (exception: UnsatisfiedLinkError) {
      throw RuntimeException("Could not load native module AudioAPIModule", exception)
    }
  }

  override fun install(): Boolean {
    MediaSessionManager.initialize(WeakReference(this), reactContext)
    
    // Get Android context and AudioManager for AEC support
    val context = reactContext.get()
    val audioManager = context?.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
    
    if (context != null && audioManager != null) {
      injectJSIBindingsWithContext(context, audioManager)
    } else {
      injectJSIBindings()
    }

    return true
  }

  override fun getDevicePreferredSampleRate(): Double = MediaSessionManager.getDevicePreferredSampleRate()

  override fun setAudioSessionActivity(
    enabled: Boolean,
    promise: Promise?,
  ) {
    promise?.resolve(true)
  }

  override fun setAudioSessionOptions(
    category: String?,
    mode: String?,
    options: ReadableArray?,
    allowHaptics: Boolean,
  ) {
    // noting to do here
  }

  override fun setLockScreenInfo(info: ReadableMap?) {
    MediaSessionManager.setLockScreenInfo(info)
  }

  override fun resetLockScreenInfo() {
    MediaSessionManager.resetLockScreenInfo()
  }

  override fun enableRemoteCommand(
    name: String?,
    enabled: Boolean,
  ) {
    MediaSessionManager.enableRemoteCommand(name!!, enabled)
  }

  override fun observeAudioInterruptions(enabled: Boolean) {
    MediaSessionManager.observeAudioInterruptions(enabled)
  }

  override fun activelyReclaimSession(enabled: Boolean) {
    MediaSessionManager.activelyReclaimSession(enabled)
  }

  override fun observeVolumeChanges(enabled: Boolean) {
    MediaSessionManager.observeVolumeChanges(enabled)
  }

  override fun requestRecordingPermissions(promise: Promise) {
    val permissionRequestListener = PermissionRequestListener(promise)
    MediaSessionManager.requestRecordingPermissions(permissionRequestListener)
  }

  override fun checkRecordingPermissions(promise: Promise) {
    promise.resolve(MediaSessionManager.checkRecordingPermissions())
  }

  override fun getDevicesInfo(promise: Promise) {
    promise.resolve(MediaSessionManager.getDevicesInfo())
  }
}
