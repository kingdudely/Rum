#ifndef MOCKTAIL_JNIVM_JNI_HELPERS_H_
#define MOCKTAIL_JNIVM_JNI_HELPERS_H_

#include "jnivm/jnivm.h"

#include "mocktail/audio/fmod_thread_floating_point.h"

#include <string>
#include <unordered_map>
#include <vector>

// Shared by the jnivm translation units in this directory. These are the
// helpers the per-concern .cc files call; the definitions all live in
// jnivm.cc. Internal to the JNI layer -- nothing outside src/jnivm should
// include this.
namespace jnivm::internal {

// Live VM owners, and the capacities of direct ByteBuffers the engine has
// handed us. Both are guarded by g_jni_state_mutex.
extern std::vector<VM*> g_live_vms;
extern std::unordered_map<jobject, jlong> g_direct_buffer_capacities;

// The engine's own native GameActivity callbacks, filled in once it starts.
extern "C" {
extern void* mocktail_gameactivity_on_start_native;
extern void* mocktail_gameactivity_on_resume_native;
extern void* mocktail_gameactivity_on_surface_created_native;
extern void* mocktail_gameactivity_on_surface_changed_native;
extern void* mocktail_gameactivity_on_surface_redraw_needed_native;
extern void* mocktail_gameactivity_on_trim_memory_native;
}

// Per-thread JVM state, installed by the JNI function tables.
extern thread_local JNIEnv* g_thread_local_env;
extern thread_local JNIEnv g_thread_env_storage;
extern thread_local VM* g_thread_vm_instance;
extern thread_local mocktail::audio::FmodThreadFloatingPointMode
    g_thread_audio_fp_mode;

// Constants
inline constexpr jsize kMaximumCookieSetCount = 128;
inline constexpr jsize kMaximumCookieSetBytes = 64 * 1024;

// Helpers
bool JniVmTraceEnabled();
VM* CurrentVM();
bool IsThreadLocalEnvValid();
const char* RobloxAndroidId();
bool StringTraceEnabled();
void Trace(const char* name);
const char* MethodName(jmethodID method_id);
jmethodID StoreMethodId(const char* name, const char* sig);
jobject ObjectResultForMethod(jmethodID method_id);
jint StaticIntResultForMethodV(jmethodID method_id, va_list args);
jint StaticIntResultForMethodA(jmethodID method_id, const jvalue* args);
jlong StaticLongResultForMethod(jmethodID method_id);
jobject ExactMessageBusStaticObject(jclass clazz, jmethodID method_id);
jobject EngineJavaCallbackObject();
bool IsJavaStringGetBytesMethod(jobject obj, jmethodID method_id);
std::string NormalizeCookieHeader(const std::string& content);
void ClearCookieString(std::string* value);
void ClearLegacyCookieStore();
void StoreCookieHeader(const std::string& cookie);
bool CookieObjectResultForMethodA(const char* name, const jvalue* args,
                                  jobject* result);
bool CookieBooleanResultForMethod(const char* name, jboolean* result);
bool FmodBooleanResultForMethod(const char* name, jboolean* result);
bool LocalStorageObjectResultForMethodA(const char* name, const jvalue* args,
                                        jobject* result);
bool LocalStorageBooleanResultForMethodV(const char* name, va_list args,
                                         jboolean* result);
bool LocalStorageBooleanResultForMethodA(const char* name, const jvalue* args,
                                         jboolean* result);
bool LocalStorageLongResultForMethod(const char* name, jlong* result);
void HandleVoidMethod(jobject obj, jmethodID method_id, va_list args);
bool HandleMemStorageCallbackVoidMethodV(jobject obj, jmethodID method_id,
                                         va_list args);
bool HandleMemStorageCallbackVoidMethodA(jobject obj, jmethodID method_id,
                                         const jvalue* args);
bool HandleRobloxCookieSetVoidMethodV(jobject obj, jmethodID method_id,
                                      va_list args);
bool HandleRobloxCookieSetVoidMethodA(jobject obj, jmethodID method_id,
                                      const jvalue* args);
jobject MakePlatformSystemDialogHandlerObject();
jobject SystemServiceObject(const std::string& service_name);
jobject ObjectResultForReceiverMethod(jobject obj, const char* name);
jobject ClassObjectForName(const std::string& requested_name);
jobject ObjectResultForMethodV(jobject obj, jmethodID method_id, va_list args);
jboolean BooleanResultForReceiverMethod(jobject obj, const char* name);
bool PackageManagerBooleanResultForMethodV(jobject obj, jmethodID method_id,
                                           va_list args, jboolean* result);
bool PackageManagerBooleanResultForMethodA(jobject obj, jmethodID method_id,
                                           const jvalue* args,
                                           jboolean* result);
jint IntResultForReceiverMethod(jobject obj, const char* name);
bool HandleWebRtcAudioManagerBooleanMethod(jobject obj, jmethodID method_id,
                                           jboolean* result);
bool HandleWebRtcAudioManagerVoidMethodV(jobject obj, jmethodID method_id,
                                         va_list args);
bool HandleWebRtcAudioManagerVoidMethodA(jobject obj, jmethodID method_id,
                                         const jvalue* args);
bool HandleRobloxExperienceLifecycleVoidMethod(jobject obj,
                                                jmethodID method_id);
bool DispatchRobloxTextInputShow(jlong text_box, jboolean show_native_input,
                                 jbyteArray text, jobject info);
void DispatchRobloxTextInputReplaceText(jstring text);
bool HandleRobloxTextInputInstanceVoidMethodV(jobject obj,
                                               jmethodID method_id,
                                               va_list args);
bool HandleRobloxTextInputInstanceVoidMethodA(jobject obj,
                                               jmethodID method_id,
                                               const jvalue* args);
bool DispatchFmodAudioDeviceInit(jobject obj, jint channels,
                                 jint sample_rate_hz,
                                 jint block_size_frames, jint block_count);
bool HandleFmodAudioDeviceBooleanMethodV(jobject obj, jmethodID method_id,
                                         va_list args, jboolean* result);
bool HandleFmodAudioDeviceBooleanMethodA(jobject obj, jmethodID method_id,
                                         const jvalue* args,
                                         jboolean* result);
bool HandleWebRtcAudioRecordIntMethodV(jobject obj, jmethodID method_id,
                                       va_list args, jint *result);
bool HandleWebRtcAudioRecordIntMethodA(jobject obj, jmethodID method_id,
                                       const jvalue *args, jint *result);
bool HandleWebRtcAudioRecordBooleanMethodV(jobject obj, jmethodID method_id,
                                           va_list args, jboolean *result);
bool HandleWebRtcAudioRecordBooleanMethodA(jobject obj, jmethodID method_id,
                                           const jvalue * /*args*/,
                                           jboolean *result);
bool HandleWebRtcAudioTrackIntMethodV(jobject obj, jmethodID method_id,
                                      va_list args, jint *result);
bool HandleWebRtcAudioTrackIntMethodA(jobject obj, jmethodID method_id,
                                      const jvalue *args, jint *result);
bool HandleWebRtcAudioTrackBooleanMethodV(jobject obj, jmethodID method_id,
                                          va_list args, jboolean *result);
bool HandleWebRtcAudioTrackBooleanMethodA(jobject obj, jmethodID method_id,
                                          const jvalue * /*args*/,
                                          jboolean *result);
bool DispatchFmodAudioDeviceWrite(jobject obj, jbyteArray array,
                                  jint requested_size);
bool HandleFmodAudioDeviceVoidMethodV(jobject obj, jmethodID method_id,
                                      va_list args);
bool HandleFmodAudioDeviceVoidMethodA(jobject obj, jmethodID method_id,
                                      const jvalue* args);
jobject StaticObjectResultForMethod(jmethodID method_id);
void HandleStaticVoidMethodV(JNIEnv *env, jclass clazz, jmethodID methodID,
                             va_list args);
void HandleStaticVoidMethodA(JNIEnv *env, jclass clazz, jmethodID methodID,
                             const jvalue *args);
bool HandleRobloxOpenWebActivityMethodV(jobject obj, jmethodID method_id,
                                        va_list args);
bool HandleRobloxOpenWebActivityMethodA(jobject obj, jmethodID method_id,
                                        const jvalue* args);
void HandleVoidMethodA(jobject obj, jmethodID method_id, const jvalue *args);
void JNICALL CallStaticVoidMethod(JNIEnv* env, jclass clazz,
                                  jmethodID methodID, ...);
jobject JNICALL CallStaticObjectMethod(JNIEnv * /*env*/, jclass clazz,
                                       jmethodID methodID, ...);
jboolean JNICALL CallStaticBooleanMethod(JNIEnv* /*env*/, jclass /*clazz*/, jmethodID methodID, ...);
jlong LongResultForReceiverMethod(jobject obj, const char* name);
jint JNICALL CallStaticIntMethod(JNIEnv * /*env*/, jclass /*clazz*/,
                                 jmethodID methodID, ...);
jlong JNICALL CallStaticLongMethod(JNIEnv * /*env*/, jclass /*clazz*/,
                                   jmethodID methodID, ...);
void JNICALL CallVoidMethod(JNIEnv * /*env*/, jobject obj, jmethodID methodID,
                            ...);
jobject JNICALL CallObjectMethod(JNIEnv* /*env*/, jobject obj,
                                 jmethodID methodID, ...);
jboolean JNICALL CallBooleanMethod(JNIEnv* /*env*/, jobject obj,
                                   jmethodID methodID, ...);
jint JNICALL CallIntMethod(JNIEnv* /*env*/, jobject obj, jmethodID methodID, ...);
jlong JNICALL CallLongMethod(JNIEnv* /*env*/, jobject obj,
                             jmethodID methodID, ...);
jobject ConstructObjectV(jclass clazz, jmethodID methodID, va_list args);
jobject ConstructObjectA(jclass clazz, jmethodID methodID, const jvalue *args);
jobject JNICALL NewObject(JNIEnv * /*env*/, jclass clazz, jmethodID methodID,
                          ...);
jbyte JNICALL CallStaticByteMethod(JNIEnv* /*env*/, jclass /*clazz*/,
                                   jmethodID /*methodID*/, ...);
jchar JNICALL CallStaticCharMethod(JNIEnv* /*env*/, jclass /*clazz*/,
                                   jmethodID /*methodID*/, ...);
jshort JNICALL CallStaticShortMethod(JNIEnv* /*env*/, jclass /*clazz*/,
                                     jmethodID /*methodID*/, ...);
jfloat JNICALL CallStaticFloatMethod(JNIEnv* /*env*/, jclass /*clazz*/,
                                     jmethodID /*methodID*/, ...);
jdouble JNICALL CallStaticDoubleMethod(JNIEnv* /*env*/, jclass /*clazz*/,
                                       jmethodID /*methodID*/, ...);
jbyte JNICALL CallByteMethod(JNIEnv* /*env*/, jobject /*obj*/,
                             jmethodID methodID, ...);
jchar JNICALL CallCharMethod(JNIEnv* /*env*/, jobject /*obj*/,
                             jmethodID /*methodID*/, ...);
jshort JNICALL CallShortMethod(JNIEnv* /*env*/, jobject /*obj*/,
                               jmethodID /*methodID*/, ...);
jfloat FloatResultForReceiverMethod(jobject obj, const char* name);
jfloat JNICALL CallFloatMethod(JNIEnv* /*env*/, jobject obj,
                               jmethodID methodID, ...);
jdouble JNICALL CallDoubleMethod(JNIEnv* /*env*/, jobject /*obj*/,
                                 jmethodID /*methodID*/, ...);

}  // namespace jnivm::internal

#endif  // MOCKTAIL_JNIVM_JNI_HELPERS_H_
