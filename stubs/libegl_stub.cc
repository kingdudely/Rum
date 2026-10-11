// Stub-inert EGL entry points for libEGL.so. A full join+frame run never
// calls any of them (verified by probe): the engine renders through Vulkan
// and resolves GLES directly against the host library. These exports exist
// because libroblox.so links 17 undefined egl* symbols; they report failure
// instead of faking a working EGL display.

#include <cstdint>

using EGLBoolean = uint32_t;
using EGLint = int32_t;
using EGLenum = uint32_t;
using EGLDisplay = void*;
using EGLConfig = void*;
using EGLContext = void*;
using EGLSurface = void*;
using EGLNativeDisplayType = void*;
using EGLNativeWindowType = void*;
using __eglMustCastFP = void (*)();

namespace {

constexpr EGLBoolean kEglFalse = 0;
constexpr EGLint kEglNotInitialized = 0x3001;

}  // namespace

extern "C" {

EGLDisplay eglGetDisplay(EGLNativeDisplayType) { return nullptr; }

EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*) { return kEglFalse; }

EGLBoolean eglBindAPI(EGLenum) { return kEglFalse; }

EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint,
                           EGLint*) {
  return kEglFalse;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig, EGLint, EGLint*) {
  return kEglFalse;
}

EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType,
                                  const EGLint*) {
  return nullptr;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const EGLint*) {
  return nullptr;
}

EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*) {
  return nullptr;
}

EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext) {
  return kEglFalse;
}

EGLContext eglGetCurrentContext() { return nullptr; }

EGLSurface eglGetCurrentSurface(EGLint) { return nullptr; }

EGLDisplay eglGetCurrentDisplay() { return nullptr; }

EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface) { return kEglFalse; }

EGLBoolean eglSwapInterval(EGLDisplay, EGLint) { return kEglFalse; }

EGLBoolean eglQuerySurface(EGLDisplay, EGLSurface, EGLint, EGLint*) {
  return kEglFalse;
}

const char* eglQueryString(EGLDisplay, EGLint) { return nullptr; }

EGLBoolean eglDestroyContext(EGLDisplay, EGLContext) { return kEglFalse; }

EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface) { return kEglFalse; }

EGLBoolean eglTerminate(EGLDisplay) { return kEglFalse; }

EGLint eglGetError() { return kEglNotInitialized; }

__eglMustCastFP eglGetProcAddress(const char*) { return nullptr; }

}  // extern "C"
