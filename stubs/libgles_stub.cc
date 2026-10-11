// SONAME placeholder for libGLESv2.so. Every GLES symbol resolves to the
// real host implementation at startup (real_gles=74, stub=0), so this
// library carries no code: it exists so the guest's DT_NEEDED entry has a
// host object to bind to. The test-only fake implementation that used to
// live here went with ROBLOX_ENABLE_TEST_GRAPHICS_STUBS; nothing runs it.
