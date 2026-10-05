#include "jnivm/jnivm.h"

#include "jni_helpers.h"
#include "jni_references.h"
#include "jni_fields.h"
#include "jni_strings.h"

#include "mocktail/audio/fmod_thread_floating_point.h"
#include "mocktail/platform/posix_primitives.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <list>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/display_size.h"


namespace jnivm {

using namespace internal;

// the JNI/JavaVM function table bindings.

void VM::InitJavaVMInterface() {
  invoke_interface_.AttachCurrentThread =
      [](JavaVM* vm, void** env, void* args) -> jint {
    if (JniVmTraceEnabled()) {
      std::cout << "  [JNI] AttachCurrentThread enter vm=" << vm
                << " env_out=" << env << '\n';
    }
    std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
    if (env != nullptr) {
      *env = nullptr;
    }
    VM* owner = VM::FromJavaVM(vm);
    if (!env || !owner) {
      if (JniVmTraceEnabled()) {
        std::cout << "  [JNI] AttachCurrentThread invalid args\n";
      }
      return JNI_EINVAL;
    }
    if (g_thread_vm_instance != owner || !IsThreadLocalEnvValid()) {
      g_thread_audio_fp_mode.Restore();
      g_thread_vm_instance = owner;
      if (!owner->jni_env_) {
        owner->jni_env_ = &owner->jni_env_storage_;
        owner->jni_env_->functions = &owner->native_interface_;
      }
      g_thread_env_storage.functions =
          owner->jni_env_->functions ? owner->jni_env_->functions
                                    : &owner->native_interface_;
      g_thread_local_env = &g_thread_env_storage;
      const auto* attach_args = static_cast<const JavaVMAttachArgs*>(args);
      if (attach_args != nullptr &&
          g_thread_audio_fp_mode.Enable(attach_args->name)) {
        std::fprintf(stderr, "  [mocktail][audio] %s: enabled FTZ/DAZ\n",
                     attach_args->name);
      }
    }
    // Reattaching to the same VM must retain any guest JNI table wrapper.
    *env = g_thread_local_env;
    if (JniVmTraceEnabled()) {
      std::cout << "  [JNI] AttachCurrentThread return env="
                << g_thread_local_env
                << '\n';
    }
    return JNI_OK;
  };

  invoke_interface_.AttachCurrentThreadAsDaemon =
      invoke_interface_.AttachCurrentThread;

  invoke_interface_.DetachCurrentThread = [](JavaVM* vm) -> jint {
    std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
    VM* owner = VM::FromJavaVM(vm);
    if (owner == nullptr) {
      return JNI_EINVAL;
    }
    if (g_thread_vm_instance != owner || !IsThreadLocalEnvValid()) {
      return JNI_EDETACHED;
    }
    g_thread_audio_fp_mode.Restore();
    g_thread_local_env = nullptr;
    g_thread_vm_instance = nullptr;
    g_thread_env_storage.functions = nullptr;
    return JNI_OK;
  };

  invoke_interface_.GetEnv =
      [](JavaVM* vm, void** env, jint version) -> jint {
    if (JniVmTraceEnabled()) {
      std::cout << "  [JNI] GetEnv enter vm=" << vm << " env_out=" << env
                << " version=" << version << '\n';
    }
    std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
    if (env != nullptr) {
      *env = nullptr;
    }
    VM* owner = VM::FromJavaVM(vm);
    if (!env || !owner) {
      if (JniVmTraceEnabled()) {
        std::cout << "  [JNI] GetEnv invalid args\n";
      }
      return JNI_EINVAL;
    }
    if (g_thread_vm_instance != owner || !IsThreadLocalEnvValid()) {
      if (JniVmTraceEnabled()) {
        std::cout << "  [JNI] GetEnv return detached\n";
      }
      return JNI_EDETACHED;
    }
    *env = g_thread_local_env;
    if (JniVmTraceEnabled()) {
      std::cout << "  [JNI] GetEnv return env=" << *env << '\n';
    }
    return JNI_OK;
  };

  invoke_interface_.DestroyJavaVM = [](JavaVM* /*vm*/) -> jint {
    return JNI_OK;
  };

  java_vm_storage_.functions = &invoke_interface_;
  java_vm_ = &java_vm_storage_;
}

// Version reporting plus the class and reflection lookups the engine uses
// to resolve a jclass from a name.
void VM::InitJNIEnvVersionAndClassInterface() {
  native_interface_.FindClass =
      [](JNIEnv* /*env*/, const char* name) -> jclass {
    if (JniVmTraceEnabled()) {
      fprintf(stderr, "  [JNI-VM] FindClass name_ptr=%p name=\"%s\" vm=%p\n",
              static_cast<const void*>(name), name ? name : "",
              static_cast<void*>(CurrentVM()));
    }
    if (TraceEnabled()) {
      std::cout << "  [JNI] FindClass: " << (name ? name : "null") << '\n';
    }
    auto cls = FallbackClassForName(name ? name : "java/lang/Object");
    return StoreClass(std::move(cls));
  };

  native_interface_.GetVersion = [](JNIEnv* /*env*/) -> jint {
    return JNI_VERSION_1_6;
  };

  native_interface_.GetObjectClass =
      [](JNIEnv* /*env*/, jobject obj) -> jclass {
    Trace("GetObjectClass");
    auto* pseudo_object = PseudoObjectFromRef(obj);
    if (pseudo_object) {
      return StoreClass(pseudo_object->GetClass());
    }
    return StoreClass(FallbackClassForName("java/lang/Object"));
  };

  native_interface_.GetSuperclass =
      [](JNIEnv* /*env*/, jclass /*sub*/) -> jclass {
    return StoreClass(FallbackClassForName("java/lang/Object"));
  };

  native_interface_.IsAssignableFrom =
      [](JNIEnv* /*env*/, jclass sub, jclass sup) -> jboolean {
    return (sub == sup || sup == nullptr) ? JNI_TRUE : JNI_TRUE;
  };

  native_interface_.IsInstanceOf =
      [](JNIEnv* /*env*/, jobject obj, jclass /*clazz*/) -> jboolean {
    return obj ? JNI_TRUE : JNI_FALSE;
  };

}

// Pending-exception state. Every other entry point checks this before
// returning, so it has to be installed first.
void VM::InitJNIEnvExceptionInterface() {
  native_interface_.ExceptionOccurred = [](JNIEnv* /*env*/) -> jthrowable {
    return nullptr;
  };

  native_interface_.ExceptionDescribe = [](JNIEnv* /*env*/) {};

  native_interface_.ExceptionClear = [](JNIEnv* /*env*/) {};

  native_interface_.ExceptionCheck = [](JNIEnv* /*env*/) -> jboolean {
    return JNI_FALSE;
  };

  native_interface_.Throw = [](JNIEnv* /*env*/, jthrowable /*obj*/) -> jint {
    return JNI_ERR;
  };

  native_interface_.ThrowNew =
      [](JNIEnv* /*env*/, jclass /*clazz*/, const char* msg) -> jint {
    if (TraceEnabled()) {
      std::cout << "  [JNI] ThrowNew: " << (msg ? msg : "") << '\n';
    }
    return JNI_ERR;
  };

  native_interface_.FatalError = [](JNIEnv* /*env*/, const char* msg) {
    std::cerr << "[JNI] FatalError: " << (msg ? msg : "") << '\n';
    std::abort();
  };

}

// Local, global and weak reference handling, plus the local-reference
// frame and the object constructors.
void VM::InitJNIEnvObjectReferenceInterface() {
  native_interface_.PushLocalFrame =
      [](JNIEnv* /*env*/, jint /*capacity*/) -> jint {
    PushLocalJniFrame();
    return JNI_OK;
  };

  native_interface_.PopLocalFrame =
      [](JNIEnv* /*env*/, jobject result) -> jobject {
    return PopLocalJniFrame(result);
  };

  native_interface_.EnsureLocalCapacity =
      [](JNIEnv* /*env*/, jint /*capacity*/) -> jint {
    return JNI_OK;
  };

  native_interface_.AllocObject =
      [](JNIEnv* /*env*/, jclass clazz) -> jobject {
    Trace("AllocObject");
    return MakeObject(clazz);
  };

  native_interface_.NewObject = NewObject;

  native_interface_.NewObjectV = [](JNIEnv * /*env*/, jclass clazz,
                                    jmethodID methodID,
                                    va_list args) -> jobject {
    if (TraceEnabled()) {
      std::cout << "  [JNI] NewObjectV: " << MethodName(methodID) << '\n';
    }
    va_list copy;
    va_copy(copy, args);
    jobject object = ConstructObjectV(clazz, methodID, copy);
    va_end(copy);
    return object;
  };

  native_interface_.NewObjectA = [](JNIEnv * /*env*/, jclass clazz,
                                    jmethodID methodID,
                                    const jvalue *args) -> jobject {
    if (TraceEnabled()) {
      std::cout << "  [JNI] NewObjectA: " << MethodName(methodID) << '\n';
    }
    return ConstructObjectA(clazz, methodID, args);
  };

  native_interface_.IsSameObject =
      [](JNIEnv* /*env*/, jobject obj1, jobject obj2) -> jboolean {
    return obj1 == obj2 ? JNI_TRUE : JNI_FALSE;
  };

  native_interface_.NewGlobalRef =
      [](JNIEnv* /*env*/, jobject obj) -> jobject {
    RetainJniReference(obj);
    return obj;
  };

  native_interface_.DeleteGlobalRef =
      [](JNIEnv* /*env*/, jobject obj) {
    ReleaseJniReference(obj);
  };

  native_interface_.NewLocalRef =
      [](JNIEnv* /*env*/, jobject obj) -> jobject {
    if (obj != nullptr) {
      RetainJniReference(obj);
      RegisterLocalRef(obj);
    }
    return obj;
  };

  native_interface_.DeleteLocalRef =
      [](JNIEnv* /*env*/, jobject obj) {
    UnregisterLocalRef(obj);
    ReleaseJniReference(obj);
  };

  native_interface_.NewWeakGlobalRef =
      [](JNIEnv* /*env*/, jobject obj) -> jweak {
    Trace("NewWeakGlobalRef");
    RetainJniReference(obj);
    return reinterpret_cast<jweak>(obj);
  };

  native_interface_.DeleteWeakGlobalRef =
      [](JNIEnv* /*env*/, jweak ref) {
    ReleaseJniReference(ref);
  };

  native_interface_.GetObjectRefType =
      [](JNIEnv* /*env*/, jobject obj) -> jobjectRefType {
    return obj ? JNILocalRefType : JNIInvalidRefType;
  };

}

// Method invocation: the plain, static and non-virtual call families in
// their fixed-width, va_list and jvalue-array forms.
// Method-id lookup and reflection. Roblox resolves some method ids
// against synthetic values that carry the method name and signature.
void VM::InitJNIEnvMethodIdInterface() {
  native_interface_.GetStaticMethodID =
      [](JNIEnv* /*env*/, jclass clazz, const char* name, const char* sig) -> jmethodID {
    auto cls = ClassFromJClass(clazz);
    if (TraceEnabled()) {
      std::cout << "  [JNI] GetStaticMethodID for class "
                << (cls ? cls->GetName() : "unknown") << ": "
                << (name ? name : "null") << " " << (sig ? sig : "null")
                << '\n';
    }
    return StoreMethodId(name, sig);
  };

  native_interface_.GetMethodID =
      [](JNIEnv* /*env*/, jclass clazz, const char* name, const char* sig) -> jmethodID {
    auto cls = ClassFromJClass(clazz);
    if (TraceEnabled()) {
      std::cout << "  [JNI] GetMethodID for class "
                << (cls ? cls->GetName() : "unknown") << ": "
                << (name ? name : "null") << " " << (sig ? sig : "null")
                << '\n';
    }
    return StoreMethodId(name, sig);
  };

  native_interface_.FromReflectedMethod =
      [](JNIEnv* /*env*/, jobject method) -> jmethodID {
    return reinterpret_cast<jmethodID>(method);
  };

  native_interface_.ToReflectedMethod =
      [](JNIEnv* /*env*/, jclass /*cls*/, jmethodID methodID,
         jboolean /*isStatic*/) -> jobject {
    return reinterpret_cast<jobject>(methodID);
  };

}

// Static method invocation, in the fixed-width, va_list and
// jvalue-array forms of each return type.
void VM::InitJNIEnvStaticMethodInterface() {
  native_interface_.CallStaticVoidMethod = CallStaticVoidMethod;

  native_interface_.CallStaticVoidMethodV =
      [](JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticVoidMethodV: " << MethodName(methodID) << '\n';
    }
    HandleStaticVoidMethodV(env, clazz, methodID, args);
  };

  native_interface_.CallStaticVoidMethodA =
      [](JNIEnv* env, jclass clazz, jmethodID methodID,
         const jvalue* args) {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticVoidMethodA: " << MethodName(methodID) << '\n';
    }
    HandleStaticVoidMethodA(env, clazz, methodID, args);
  };

  native_interface_.CallStaticObjectMethod = CallStaticObjectMethod;

  native_interface_.CallStaticObjectMethodV = [](JNIEnv * /*env*/, jclass clazz,
                                                 jmethodID methodID,
                                                 va_list args) -> jobject {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticObjectMethodV: " << MethodName(methodID)
                << '\n';
    }
    jobject result = ObjectResultForMethodV(nullptr, methodID, args);
    if (result != nullptr) {
      return result;
    }
    result = ExactMessageBusStaticObject(clazz, methodID);
    return result != nullptr ? result : StaticObjectResultForMethod(methodID);
  };

  native_interface_.CallStaticObjectMethodA =
      [](JNIEnv * /*env*/, jclass clazz, jmethodID methodID,
         const jvalue *args) -> jobject {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticObjectMethodA: " << MethodName(methodID)
                << '\n';
    }
    const char *name = MethodName(methodID);
    if (args && std::strcmp(name, "forName") == 0) {
      return ClassObjectForName(
          StringFromJString(reinterpret_cast<jstring>(args[0].l)));
    }
    jobject cookie_result = nullptr;
    if (CookieObjectResultForMethodA(name, args, &cookie_result)) {
      return cookie_result;
    }
    jobject message_bus = ExactMessageBusStaticObject(clazz, methodID);
    if (message_bus != nullptr) {
      return message_bus;
    }
    return StaticObjectResultForMethod(methodID);
  };

  native_interface_.CallStaticBooleanMethod = CallStaticBooleanMethod;

  native_interface_.CallStaticBooleanMethodV =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID methodID, va_list /*args*/) -> jboolean {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticBooleanMethodV: " << MethodName(methodID) << '\n';
    }
    jboolean result = JNI_FALSE;
    const char* name = MethodName(methodID);
    if (std::strcmp(name, "isSystemThemeAvailable") == 0) {
      return JNI_TRUE;
    }
    if (FmodBooleanResultForMethod(name, &result)) {
      return result;
    }
    return CookieBooleanResultForMethod(name, &result) ? result : JNI_FALSE;
  };

  native_interface_.CallStaticBooleanMethodA =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID methodID,
         const jvalue* /*args*/) -> jboolean {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticBooleanMethodA: " << MethodName(methodID) << '\n';
    }
    jboolean result = JNI_FALSE;
    const char* name = MethodName(methodID);
    if (std::strcmp(name, "isSystemThemeAvailable") == 0) {
      return JNI_TRUE;
    }
    if (FmodBooleanResultForMethod(name, &result)) {
      return result;
    }
    return CookieBooleanResultForMethod(name, &result) ? result : JNI_FALSE;
  };

  native_interface_.CallStaticByteMethod = CallStaticByteMethod;
  native_interface_.CallStaticByteMethodV =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jbyte { return 0; };
  native_interface_.CallStaticByteMethodA =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         const jvalue* /*args*/) -> jbyte { return 0; };

  native_interface_.CallStaticCharMethod = CallStaticCharMethod;
  native_interface_.CallStaticCharMethodV =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jchar { return 0; };
  native_interface_.CallStaticCharMethodA =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         const jvalue* /*args*/) -> jchar { return 0; };

  native_interface_.CallStaticShortMethod = CallStaticShortMethod;
  native_interface_.CallStaticShortMethodV =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jshort { return 0; };
  native_interface_.CallStaticShortMethodA =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         const jvalue* /*args*/) -> jshort { return 0; };

  native_interface_.CallStaticIntMethod = CallStaticIntMethod;

  native_interface_.CallStaticIntMethodV =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID methodID, va_list args) -> jint {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticIntMethodV: " << MethodName(methodID) << '\n';
    }
    return StaticIntResultForMethodV(methodID, args);
  };

  native_interface_.CallStaticIntMethodA =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID methodID,
         const jvalue* args) -> jint {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticIntMethodA: " << MethodName(methodID) << '\n';
    }
    return StaticIntResultForMethodA(methodID, args);
  };

  native_interface_.CallStaticLongMethod = CallStaticLongMethod;

  native_interface_.CallStaticLongMethodV =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jmethodID methodID, va_list /*args*/) -> jlong {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticLongMethodV: " << MethodName(methodID) << '\n';
    }
    return StaticLongResultForMethod(methodID);
  };

  native_interface_.CallStaticLongMethodA =
      [](JNIEnv * /*env*/, jclass /*clazz*/, jmethodID methodID,
         const jvalue * /*args*/) -> jlong {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallStaticLongMethodA: " << MethodName(methodID) << '\n';
    }
    return StaticLongResultForMethod(methodID);
  };

  native_interface_.CallStaticFloatMethod = CallStaticFloatMethod;
  native_interface_.CallStaticFloatMethodV =
      [](JNIEnv * /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jfloat { return 0.0f; };
  native_interface_.CallStaticFloatMethodA =
      [](JNIEnv * /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         const jvalue * /*args*/) -> jfloat { return 0.0f; };

  native_interface_.CallStaticDoubleMethod = CallStaticDoubleMethod;
  native_interface_.CallStaticDoubleMethodV =
      [](JNIEnv * /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jdouble { return 0.0; };
  native_interface_.CallStaticDoubleMethodA =
      [](JNIEnv * /*env*/, jclass /*clazz*/, jmethodID /*methodID*/,
         const jvalue * /*args*/) -> jdouble { return 0.0; };

}

// Instance method invocation, in the same three forms.
void VM::InitJNIEnvInstanceMethodInterface() {
  native_interface_.CallVoidMethod = CallVoidMethod;

  native_interface_.CallVoidMethodV = [](JNIEnv * /*env*/, jobject obj,
                                         jmethodID methodID, va_list args) {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallVoidMethodV: " << MethodName(methodID) << '\n';
    }
    if (!HandleRobloxExperienceLifecycleVoidMethod(obj, methodID) &&
        !HandleWebRtcAudioManagerVoidMethodV(obj, methodID, args) &&
        !HandleRobloxOpenWebActivityMethodV(obj, methodID, args) &&
        !HandleRobloxTextInputInstanceVoidMethodV(obj, methodID, args) &&
        !HandleFmodAudioDeviceVoidMethodV(obj, methodID, args) &&
        !HandleRobloxCookieSetVoidMethodV(obj, methodID, args) &&
        !HandleMemStorageCallbackVoidMethodV(obj, methodID, args)) {
      HandleVoidMethod(obj, methodID, args);
    }
  };

  native_interface_.CallVoidMethodA = [](JNIEnv * /*env*/, jobject obj,
                                         jmethodID methodID,
                                         const jvalue *args) {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallVoidMethodA: " << MethodName(methodID) << '\n';
    }
    if (!HandleRobloxExperienceLifecycleVoidMethod(obj, methodID) &&
        !HandleWebRtcAudioManagerVoidMethodA(obj, methodID, args) &&
        !HandleRobloxOpenWebActivityMethodA(obj, methodID, args) &&
        !HandleRobloxTextInputInstanceVoidMethodA(obj, methodID, args) &&
        !HandleFmodAudioDeviceVoidMethodA(obj, methodID, args) &&
        !HandleRobloxCookieSetVoidMethodA(obj, methodID, args) &&
        !HandleMemStorageCallbackVoidMethodA(obj, methodID, args)) {
      HandleVoidMethodA(obj, methodID, args);
    }
  };

  native_interface_.CallObjectMethod = CallObjectMethod;

  native_interface_.CallObjectMethodV =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID,
         va_list args) -> jobject {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallObjectMethodV: " << MethodName(methodID) << '\n';
    }
    return ObjectResultForMethodV(obj, methodID, args);
  };

  native_interface_.CallObjectMethodA =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID,
         const jvalue* args) -> jobject {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallObjectMethodA: " << MethodName(methodID) << '\n';
    }
    const char* name = MethodName(methodID);
    if (IsJavaStringGetBytesMethod(obj, methodID)) {
      return args != nullptr
                 ? JavaStringGetUtf8Bytes(
                       obj, static_cast<jstring>(args[0].l))
                 : nullptr;
    }
    if (args && std::strcmp(name, "run") == 0 &&
        ObjectClassName(obj) ==
            "com/roblox/universalapp/messagebus/RequestHandlerRaw") {
      VM* vm = CurrentVM();
      return vm != nullptr
                 ? vm->DispatchMessageBusRequestHandler(
                       obj, vm->GetJNIEnv(), static_cast<jstring>(args[0].l))
                 : nullptr;
    }
    jobject local_storage_result = nullptr;
    if (LocalStorageObjectResultForMethodA(name, args, &local_storage_result)) {
      return local_storage_result;
    }
    jobject cookie_result = nullptr;
    if (CookieObjectResultForMethodA(name, args, &cookie_result)) {
      return cookie_result;
    }
    if (args && std::strcmp(name, "getSystemService") == 0) {
      return SystemServiceObject(StringFromJString(
          reinterpret_cast<jstring>(args[0].l)));
    }
    if (args && (std::strcmp(name, "loadClass") == 0 ||
                 std::strcmp(name, "findClass") == 0)) {
      return ClassObjectForName(StringFromJString(
          reinterpret_cast<jstring>(args[0].l)));
    }
    if (args && std::strcmp(name, "getString") == 0) {
      return args[1].l ? args[1].l : MakeString("");
    }
    jobject receiver_result = ObjectResultForReceiverMethod(obj, name);
    return receiver_result ? receiver_result : ObjectResultForMethod(methodID);
  };

  native_interface_.CallBooleanMethod = CallBooleanMethod;

  native_interface_.CallBooleanMethodV =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID,
         va_list args) -> jboolean {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallBooleanMethodV: " << MethodName(methodID) << '\n';
    }
    jboolean result = JNI_FALSE;
    if (HandleWebRtcAudioManagerBooleanMethod(obj, methodID, &result)) {
      return result;
    }
    if (HandleWebRtcAudioRecordBooleanMethodV(obj, methodID, args, &result)) {
      return result;
    }
    if (HandleWebRtcAudioTrackBooleanMethodV(obj, methodID, args, &result)) {
      return result;
    }
    if (HandleFmodAudioDeviceBooleanMethodV(obj, methodID, args, &result)) {
      return result;
    }
    if (PackageManagerBooleanResultForMethodV(obj, methodID, args, &result)) {
      return result;
    }
    if (LocalStorageBooleanResultForMethodV(MethodName(methodID), args,
                                            &result)) {
      return result;
    }
    if (CookieBooleanResultForMethod(MethodName(methodID), &result)) {
      return result;
    }
    return BooleanResultForReceiverMethod(obj, MethodName(methodID));
  };

  native_interface_.CallBooleanMethodA =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID,
         const jvalue* args) -> jboolean {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallBooleanMethodA: " << MethodName(methodID) << '\n';
    }
    jboolean result = JNI_FALSE;
    if (HandleWebRtcAudioManagerBooleanMethod(obj, methodID, &result)) {
      return result;
    }
    if (HandleWebRtcAudioRecordBooleanMethodA(obj, methodID, args, &result)) {
      return result;
    }
    if (HandleWebRtcAudioTrackBooleanMethodA(obj, methodID, args, &result)) {
      return result;
    }
    if (HandleFmodAudioDeviceBooleanMethodA(obj, methodID, args, &result)) {
      return result;
    }
    if (PackageManagerBooleanResultForMethodA(obj, methodID, args, &result)) {
      return result;
    }
    if (LocalStorageBooleanResultForMethodA(MethodName(methodID), args,
                                            &result)) {
      return result;
    }
    if (CookieBooleanResultForMethod(MethodName(methodID), &result)) {
      return result;
    }
    return BooleanResultForReceiverMethod(obj, MethodName(methodID));
  };

  native_interface_.CallByteMethod = CallByteMethod;
  native_interface_.CallByteMethodV =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jbyte { return 0; };
  native_interface_.CallByteMethodA =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         const jvalue* /*args*/) -> jbyte { return 0; };

  native_interface_.CallCharMethod = CallCharMethod;
  native_interface_.CallCharMethodV =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jchar { return 0; };
  native_interface_.CallCharMethodA =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         const jvalue* /*args*/) -> jchar { return 0; };

  native_interface_.CallShortMethod = CallShortMethod;
  native_interface_.CallShortMethodV =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jshort { return 0; };
  native_interface_.CallShortMethodA =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         const jvalue* /*args*/) -> jshort { return 0; };

  native_interface_.CallIntMethod = CallIntMethod;

  native_interface_.CallIntMethodV = [](JNIEnv * /*env*/, jobject obj,
                                        jmethodID methodID,
                                        va_list args) -> jint {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallIntMethodV: " << MethodName(methodID) << '\n';
    }
    jint result = 0;
    if (HandleWebRtcAudioRecordIntMethodV(obj, methodID, args, &result) ||
        HandleWebRtcAudioTrackIntMethodV(obj, methodID, args, &result)) {
      return result;
    }
    return IntResultForReceiverMethod(obj, MethodName(methodID));
  };

  native_interface_.CallIntMethodA = [](JNIEnv * /*env*/, jobject obj,
                                        jmethodID methodID,
                                        const jvalue *args) -> jint {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallIntMethodA: " << MethodName(methodID) << '\n';
    }
    jint result = 0;
    if (HandleWebRtcAudioRecordIntMethodA(obj, methodID, args, &result) ||
        HandleWebRtcAudioTrackIntMethodA(obj, methodID, args, &result)) {
      return result;
    }
    return IntResultForReceiverMethod(obj, MethodName(methodID));
  };

  native_interface_.CallLongMethod = CallLongMethod;

  native_interface_.CallLongMethodV =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID, va_list /*args*/) -> jlong {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallLongMethodV: " << MethodName(methodID) << '\n';
    }
    jlong result = 0;
    if (LocalStorageLongResultForMethod(MethodName(methodID), &result)) {
      return result;
    }
    return LongResultForReceiverMethod(obj, MethodName(methodID));
  };

  native_interface_.CallLongMethodA =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID,
         const jvalue* /*args*/) -> jlong {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallLongMethodA: " << MethodName(methodID) << '\n';
    }
    jlong result = 0;
    if (LocalStorageLongResultForMethod(MethodName(methodID), &result)) {
      return result;
    }
    return LongResultForReceiverMethod(obj, MethodName(methodID));
  };

  native_interface_.CallFloatMethod = CallFloatMethod;
  native_interface_.CallFloatMethodV =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID,
         va_list /*args*/) -> jfloat {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallFloatMethodV: " << MethodName(methodID)
                << '\n';
    }
    return FloatResultForReceiverMethod(obj, MethodName(methodID));
  };
  native_interface_.CallFloatMethodA =
      [](JNIEnv* /*env*/, jobject obj, jmethodID methodID,
         const jvalue* /*args*/) -> jfloat {
    if (TraceEnabled()) {
      std::cout << "  [JNI] CallFloatMethodA: " << MethodName(methodID)
                << '\n';
    }
    return FloatResultForReceiverMethod(obj, MethodName(methodID));
  };

  native_interface_.CallDoubleMethod = CallDoubleMethod;
  native_interface_.CallDoubleMethodV =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         va_list /*args*/) -> jdouble { return 0.0; };
  native_interface_.CallDoubleMethodA =
      [](JNIEnv* /*env*/, jobject /*obj*/, jmethodID /*methodID*/,
         const jvalue* /*args*/) -> jdouble { return 0.0; };

}

// Method calls, split by how the method is reached.
void VM::InitJNIEnvMethodInterface() {
  InitJNIEnvMethodIdInterface();
  InitJNIEnvStaticMethodInterface();
  InitJNIEnvInstanceMethodInterface();
}

// Field reads and writes, in the plain and static forms. Roblox resolves
// some of these against synthetic field ids that carry the field name.
// Field-id lookup and reflection.
void VM::InitJNIEnvFieldIdInterface() {
  native_interface_.GetStaticFieldID =
      [](JNIEnv* /*env*/, jclass clazz, const char* name, const char* sig) -> jfieldID {
    auto cls = ClassFromJClass(clazz);
    if (TraceEnabled()) {
      std::cout << "  [JNI] GetStaticFieldID for class "
                << (cls ? cls->GetName() : "unknown") << ": "
                << (name ? name : "null") << " " << (sig ? sig : "null")
                << '\n';
    }
    return StoreFieldId(name);
  };

  native_interface_.GetFieldID =
      [](JNIEnv* /*env*/, jclass clazz, const char* name, const char* sig) -> jfieldID {
    auto cls = ClassFromJClass(clazz);
    if (TraceEnabled()) {
      std::cout << "  [JNI] GetFieldID for class "
                << (cls ? cls->GetName() : "unknown") << ": "
                << (name ? name : "null") << " " << (sig ? sig : "null")
                << '\n';
    }
    return StoreFieldId(name);
  };

  native_interface_.FromReflectedField =
      [](JNIEnv* /*env*/, jobject field) -> jfieldID {
    return reinterpret_cast<jfieldID>(field);
  };

  native_interface_.ToReflectedField =
      [](JNIEnv* /*env*/, jclass /*cls*/, jfieldID fieldID,
         jboolean /*isStatic*/) -> jobject {
    return reinterpret_cast<jobject>(fieldID);
  };

}

// Static field reads and writes for every type.
void VM::InitJNIEnvStaticFieldInterface() {
  native_interface_.GetStaticObjectField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID fieldID) -> jobject {
    if (TraceEnabled()) {
      auto* name = reinterpret_cast<const char*>(fieldID);
      std::cout << "  [JNI] GetStaticObjectField: "
                << (name ? name : "unknown") << '\n';
    }
    auto* name = reinterpret_cast<const char*>(fieldID);
    if (name && std::strcmp(name, "ANDROID_ID") == 0) {
      return MakeString(RobloxAndroidId());
    }
    if (name && std::strcmp(name, "INSTANCE") == 0) {
      return MakePlatformSystemDialogHandlerObject();
    }
    if (name && std::strcmp(name, "sImplementation") == 0) {
      return EngineJavaCallbackObject();
    }
    return StaticObjectFieldValue(name);
  };

  native_interface_.GetStaticBooleanField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jboolean {
    return JNI_FALSE;
  };
  native_interface_.GetStaticByteField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jbyte {
    return 0;
  };
  native_interface_.GetStaticCharField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jchar {
    return 0;
  };
  native_interface_.GetStaticShortField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jshort {
    return 0;
  };
  native_interface_.GetStaticIntField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jint {
    return 0;
  };
  native_interface_.GetStaticLongField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jlong {
    return 0;
  };
  native_interface_.GetStaticFloatField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jfloat {
    return 0.0f;
  };
  native_interface_.GetStaticDoubleField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/) -> jdouble {
    return 0.0;
  };

  native_interface_.SetStaticObjectField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID fieldID,
         jobject value) {
    SetStaticObjectFieldRaw(reinterpret_cast<const char*>(fieldID), value);
  };
  native_interface_.SetStaticBooleanField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jboolean /*value*/) {};
  native_interface_.SetStaticByteField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jbyte /*value*/) {};
  native_interface_.SetStaticCharField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jchar /*value*/) {};
  native_interface_.SetStaticShortField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jshort /*value*/) {};
  native_interface_.SetStaticIntField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jint /*value*/) {};
  native_interface_.SetStaticLongField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jlong /*value*/) {};
  native_interface_.SetStaticFloatField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jfloat /*value*/) {};
  native_interface_.SetStaticDoubleField =
      [](JNIEnv* /*env*/, jclass /*clazz*/, jfieldID /*fieldID*/,
         jdouble /*value*/) {};

}

// Instance field reads and writes for every type.
void VM::InitJNIEnvInstanceFieldInterface() {
  native_interface_.GetObjectField =
      [](JNIEnv* env, jobject obj, jfieldID fieldID) -> jobject {
    auto* name = reinterpret_cast<const char*>(fieldID);
    jobject value = ObjectFieldValue(obj, name);
    if (TraceEnabled()) {
      std::cout << "  [JNI] GetObjectField: "
                << (name ? name : "unknown") << " -> " << value << '\n';
    }
    return value;
  };
  native_interface_.GetBooleanField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID) -> jboolean {
    return BooleanFieldValue(obj, reinterpret_cast<const char*>(fieldID));
  };
  native_interface_.GetByteField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/) -> jbyte {
    return 0;
  };
  native_interface_.GetCharField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/) -> jchar {
    return 0;
  };
  native_interface_.GetShortField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/) -> jshort {
    return 0;
  };
  native_interface_.GetIntField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID) -> jint {
    return IntFieldValue(obj, reinterpret_cast<const char*>(fieldID));
  };
  native_interface_.GetLongField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID) -> jlong {
    return LongFieldValue(obj, reinterpret_cast<const char*>(fieldID));
  };
  native_interface_.GetFloatField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID) -> jfloat {
    return FloatFieldValue(obj, reinterpret_cast<const char*>(fieldID));
  };
  native_interface_.GetDoubleField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/) -> jdouble {
    return 0.0;
  };

  native_interface_.SetObjectField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID,
         jobject val) {
    SetObjectFieldRaw(obj, reinterpret_cast<const char*>(fieldID), val);
  };
  native_interface_.SetBooleanField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID,
         jboolean val) {
    SetBooleanFieldRaw(obj, reinterpret_cast<const char*>(fieldID), val);
  };
  native_interface_.SetByteField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/, jbyte /*val*/) {};
  native_interface_.SetCharField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/, jchar /*val*/) {};
  native_interface_.SetShortField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/, jshort /*val*/) {};
  native_interface_.SetIntField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID, jint val) {
    SetIntFieldRaw(obj, reinterpret_cast<const char*>(fieldID), val);
  };
  native_interface_.SetLongField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID, jlong val) {
    SetLongFieldRaw(obj, reinterpret_cast<const char*>(fieldID), val);
  };
  native_interface_.SetFloatField =
      [](JNIEnv* /*env*/, jobject obj, jfieldID fieldID, jfloat val) {
    SetFloatFieldRaw(obj, reinterpret_cast<const char*>(fieldID), val);
  };
  native_interface_.SetDoubleField =
      [](JNIEnv* /*env*/, jobject /*obj*/, jfieldID /*fieldID*/, jdouble /*val*/) {};

}

// Field access, split by how the field is reached.
void VM::InitJNIEnvFieldInterface() {
  InitJNIEnvFieldIdInterface();
  InitJNIEnvStaticFieldInterface();
  InitJNIEnvInstanceFieldInterface();
}

// UTF-16 and modified-UTF-8 string construction, access and release.
void VM::InitJNIEnvStringInterface() {
  native_interface_.NewStringUTF =
      [](JNIEnv* /*env*/, const char* utf) -> jstring {
    return MakeString(utf);
  };

  native_interface_.GetStringUTFLength =
      [](JNIEnv* /*env*/, jstring str) -> jsize {
    return StringModifiedUtf8Length(str);
  };

  native_interface_.GetStringUTFChars =
      [](JNIEnv* /*env*/, jstring str, jboolean* isCopy) -> const char* {
    if (isCopy) *isCopy = JNI_FALSE;
    const char* result = StringChars(str);
    if (StringTraceEnabled()) {
      const std::size_t length =
          static_cast<std::size_t>(StringModifiedUtf8Length(str));
      fprintf(stderr,
              "  [JNI] GetStringUTFChars str=%p chars=%p len=%zu\n",
              static_cast<void*>(str), static_cast<const void*>(result),
              length);
    }
    return result;
  };

  native_interface_.ReleaseStringUTFChars =
      [](JNIEnv* /*env*/, jstring /*str*/, const char* /*chars*/) {};

  native_interface_.NewString =
      [](JNIEnv* /*env*/, const jchar* unicode, jsize len) -> jstring {
    return MakeUtf16String(unicode, len);
  };

  native_interface_.GetStringLength =
      [](JNIEnv* /*env*/, jstring str) -> jsize {
    return StringUtf16Length(str);
  };

  native_interface_.GetStringChars =
      [](JNIEnv* /*env*/, jstring str, jboolean* isCopy) -> const jchar* {
    if (isCopy) *isCopy = JNI_FALSE;
    return StringUtf16Chars(str);
  };

  native_interface_.ReleaseStringChars =
      [](JNIEnv* /*env*/, jstring /*str*/, const jchar* /*chars*/) {};

  native_interface_.GetStringUTFRegion =
      [](JNIEnv* /*env*/, jstring str, jsize start, jsize len, char* buf) {
    CopyStringModifiedUtf8Region(str, start, len, buf);
  };

  native_interface_.GetStringRegion =
      [](JNIEnv* /*env*/, jstring str, jsize start, jsize len, jchar* buf) {
    CopyStringRegion(str, start, len, buf);
  };

}

// Array construction and access. Primitive arrays are backed by the
// PseudoArray the engine handed us, not by guest memory.
void VM::InitJNIEnvArrayInterface() {
  native_interface_.GetArrayLength =
      [](JNIEnv* /*env*/, jarray array) -> jsize {
    PseudoArray* pseudo_array = ArrayFromRef(array);
    if (!pseudo_array) {
      return 0;
    }
    if (!pseudo_array->bytes.empty()) {
      return static_cast<jsize>(pseudo_array->bytes.size());
    }
    if (!pseudo_array->floats.empty()) {
      return static_cast<jsize>(pseudo_array->floats.size());
    }
    return static_cast<jsize>(pseudo_array->objects.size());
  };

  native_interface_.NewObjectArray =
      [](JNIEnv* /*env*/, jsize len, jclass /*clazz*/,
         jobject init) -> jobjectArray {
    Trace("NewObjectArray");
    return MakeObjectArray(len, init);
  };

  native_interface_.GetObjectArrayElement =
      [](JNIEnv* env, jobjectArray array, jsize index) -> jobject {
    PseudoArray* pseudo_array = ArrayFromRef(array);
    if (!pseudo_array || index < 0 ||
        static_cast<std::size_t>(index) >= pseudo_array->objects.size()) {
      return nullptr;
    }
    jobject obj = pseudo_array->objects[static_cast<std::size_t>(index)];
    return env != nullptr && obj != nullptr ? env->NewLocalRef(obj) : obj;
  };

  native_interface_.SetObjectArrayElement =
      [](JNIEnv* env, jobjectArray array, jsize index, jobject val) {
    PseudoArray* pseudo_array = ArrayFromRef(array);
    if (!pseudo_array || index < 0 ||
        static_cast<std::size_t>(index) >= pseudo_array->objects.size()) {
      return;
    }
    if (val != nullptr) {
      RetainJniReference(val);
    }
    jobject prev = pseudo_array->objects[static_cast<std::size_t>(index)];
    pseudo_array->objects[static_cast<std::size_t>(index)] = val;
    if (prev != nullptr) {
      ReleaseJniReference(prev);
    }
  };

  native_interface_.NewByteArray =
      [](JNIEnv* /*env*/, jsize len) -> jbyteArray {
    Trace("NewByteArray");
    return MakeByteArray(len);
  };

  native_interface_.GetByteArrayElements =
      [](JNIEnv* /*env*/, jbyteArray array, jboolean* isCopy) -> jbyte* {
    if (isCopy) *isCopy = JNI_FALSE;
    PseudoArray* pseudo_array = ArrayFromRef(array);
    return pseudo_array && !pseudo_array->bytes.empty()
               ? pseudo_array->bytes.data()
               : nullptr;
  };

  native_interface_.ReleaseByteArrayElements =
      [](JNIEnv* /*env*/, jbyteArray /*array*/, jbyte* /*elems*/,
         jint /*mode*/) {};

  native_interface_.NewFloatArray =
      [](JNIEnv* /*env*/, jsize len) -> jfloatArray {
    Trace("NewFloatArray");
    return MakeFloatArray(len);
  };

  native_interface_.GetFloatArrayElements =
      [](JNIEnv* /*env*/, jfloatArray array, jboolean* isCopy) -> jfloat* {
    if (isCopy) *isCopy = JNI_FALSE;
    PseudoArray* pseudo_array = ArrayFromRef(array);
    return pseudo_array && !pseudo_array->floats.empty()
               ? pseudo_array->floats.data()
               : nullptr;
  };

  native_interface_.ReleaseFloatArrayElements =
      [](JNIEnv* /*env*/, jfloatArray /*array*/, jfloat* /*elems*/,
         jint /*mode*/) {};

  native_interface_.GetByteArrayRegion =
      [](JNIEnv* /*env*/, jbyteArray array, jsize start, jsize len, jbyte* buf) {
    PseudoArray* pseudo_array = ArrayFromRef(array);
    if (!pseudo_array || !buf || start < 0 || len <= 0) {
      return;
    }
    std::size_t offset = static_cast<std::size_t>(start);
    if (offset >= pseudo_array->bytes.size()) {
      return;
    }
    std::size_t count =
        std::min(static_cast<std::size_t>(len),
                 pseudo_array->bytes.size() - offset);
    std::memcpy(buf, pseudo_array->bytes.data() + offset, count);
  };

  native_interface_.SetByteArrayRegion =
      [](JNIEnv* /*env*/, jbyteArray array, jsize start, jsize len,
         const jbyte* buf) {
    PseudoArray* pseudo_array = ArrayFromRef(array);
    if (!pseudo_array || !buf || start < 0 || len <= 0) {
      return;
    }
    std::size_t offset = static_cast<std::size_t>(start);
    if (offset >= pseudo_array->bytes.size()) {
      return;
    }
    std::size_t count =
        std::min(static_cast<std::size_t>(len),
                 pseudo_array->bytes.size() - offset);
    std::memcpy(pseudo_array->bytes.data() + offset, buf, count);
  };

  native_interface_.SetFloatArrayRegion =
      [](JNIEnv* /*env*/, jfloatArray array, jsize start, jsize len,
         const jfloat* buf) {
    PseudoArray* pseudo_array = ArrayFromRef(array);
    if (!pseudo_array || !buf || start < 0 || len <= 0) {
      return;
    }
    std::size_t offset = static_cast<std::size_t>(start);
    if (offset >= pseudo_array->floats.size()) {
      return;
    }
    std::size_t count =
        std::min(static_cast<std::size_t>(len),
                 pseudo_array->floats.size() - offset);
    std::memcpy(pseudo_array->floats.data() + offset, buf,
                count * sizeof(jfloat));
  };

  native_interface_.GetPrimitiveArrayCritical =
      [](JNIEnv* env, jarray array, jboolean* isCopy) -> void* {
    if (isCopy) *isCopy = JNI_FALSE;
    PseudoArray* pseudo_array = ArrayFromRef(array);
    if (!pseudo_array) {
      return nullptr;
    }
    if (!pseudo_array->bytes.empty()) {
      return pseudo_array->bytes.data();
    }
    return !pseudo_array->floats.empty() ? pseudo_array->floats.data()
                                         : nullptr;
  };

  native_interface_.ReleasePrimitiveArrayCritical =
      [](JNIEnv* /*env*/, jarray /*array*/, void* /*carray*/, jint /*mode*/) {};

}

// Native method registration, direct byte buffers, monitors, and the
// back-pointer to the owning VM.
void VM::InitJNIEnvMiscInterface() {
	  native_interface_.RegisterNatives =
	      [](JNIEnv* /*env*/, jclass clazz, const JNINativeMethod* methods, jint nMethods) -> jint {
	    auto cls = ClassFromJClass(clazz);
	    if (methods == nullptr || nMethods <= 0) {
	      return JNI_OK;
	    }
	    for (int i = 0; i < nMethods; ++i) {
	      const char* name = methods[i].name;
	      if (name == nullptr) {
	        continue;
	      }
	      struct GameActivityBinding { const char* method; void** out; };
	      const GameActivityBinding game_activity_bindings[] = {
	          {"onStartNative", &mocktail_gameactivity_on_start_native},
	          {"onResumeNative", &mocktail_gameactivity_on_resume_native},
	          {"onSurfaceCreatedNative", &mocktail_gameactivity_on_surface_created_native},
	          {"onSurfaceChangedNative", &mocktail_gameactivity_on_surface_changed_native},
	          {"onSurfaceRedrawNeededNative", &mocktail_gameactivity_on_surface_redraw_needed_native},
	          {"onTrimMemoryNative", &mocktail_gameactivity_on_trim_memory_native},
	      };
	      for (const auto& b : game_activity_bindings) {
	        if (std::strcmp(name, b.method) == 0) {
	          *b.out = methods[i].fnPtr;
	          break;
	        }
	      }
	      if (cls != nullptr) {
	        const std::string kWebrtcBase = "org/webrtc/voiceengine/";
	        const std::string class_name = cls->GetName();
	        if (class_name.size() > kWebrtcBase.size() &&
	            class_name.compare(0, kWebrtcBase.size(), kWebrtcBase) == 0) {
	          VM* vm = CurrentVM();
	          if (vm != nullptr) {
	            const std::string tail = class_name.substr(kWebrtcBase.size());
	            if (tail == "WebRtcAudioManager") {
	              vm->RegisterWebRtcAudioManagerNative(
	                  methods[i].name, methods[i].signature, methods[i].fnPtr);
	            } else if (tail == "WebRtcAudioRecord") {
	              vm->RegisterWebRtcAudioRecordNative(
	                  methods[i].name, methods[i].signature, methods[i].fnPtr);
	            } else if (tail == "WebRtcAudioTrack") {
	              vm->RegisterWebRtcAudioTrackNative(
	                  methods[i].name, methods[i].signature, methods[i].fnPtr);
	            }
	          }
	        }
	      }
            }
	    if (TraceEnabled()) {
	      std::cout << "  [JNI] RegisterNatives for class "
	                << (cls ? cls->GetName() : "unknown") << " ("
                << nMethods << " methods):\n";
      for (int i = 0; i < nMethods; ++i) {
        std::cout << "    " << methods[i].name << " "
                  << methods[i].signature << " -> " << methods[i].fnPtr
                  << '\n';
      }
    }
    return JNI_OK;
  };

  native_interface_.UnregisterNatives =
      [](JNIEnv* /*env*/, jclass /*clazz*/) -> jint {
    return JNI_OK;
  };

  native_interface_.NewDirectByteBuffer = [](JNIEnv * /*env*/, void *address,
                                             jlong capacity) -> jobject {
    if (address == nullptr || capacity < 0) {
      return nullptr;
    }
    const jobject buffer = reinterpret_cast<jobject>(address);
    std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
    g_direct_buffer_capacities[buffer] = capacity;
    return buffer;
  };

  native_interface_.GetDirectBufferAddress =
      [](JNIEnv* /*env*/, jobject buf) -> void* {
    return reinterpret_cast<void*>(buf);
  };

  native_interface_.GetDirectBufferCapacity = [](JNIEnv * /*env*/,
                                                 jobject buf) -> jlong {
    std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
    const auto found = g_direct_buffer_capacities.find(buf);
    return found != g_direct_buffer_capacities.end() ? found->second : -1;
  };

  native_interface_.MonitorEnter =
      [](JNIEnv* /*env*/, jobject /*obj*/) -> jint {
    return JNI_OK;
  };

  native_interface_.MonitorExit =
      [](JNIEnv* /*env*/, jobject /*obj*/) -> jint {
    return JNI_OK;
  };

  native_interface_.GetJavaVM =
      [](JNIEnv* env, JavaVM** vm) -> jint {
    VM* current_vm = CurrentVM();
    if (!vm || !current_vm) {
      return JNI_ERR;
    }
    *vm = current_vm->GetJavaVM();
    if (TraceEnabled() || JniVmTraceEnabled()) {
      std::cout << "  [JNI] GetJavaVM env=" << env << " out=" << vm
                << " vm=" << *vm << '\n';
    }
    return JNI_OK;
  };

}

// Installs every JNI entry point the engine can reach, grouped by the
// interface it belongs to, then points JNIEnv* at the assembled table.
void VM::InitJNIFunctionTables() {
  InitJavaVMInterface();
  InitJNIEnvVersionAndClassInterface();
  InitJNIEnvExceptionInterface();
  InitJNIEnvObjectReferenceInterface();
  InitJNIEnvMethodInterface();
  InitJNIEnvFieldInterface();
  InitJNIEnvStringInterface();
  InitJNIEnvArrayInterface();
  InitJNIEnvMiscInterface();

  jni_env_ = &jni_env_storage_;
  jni_env_->functions = &native_interface_;
}

}  // namespace jnivm
