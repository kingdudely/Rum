#ifndef MOCKTAIL_JNIVM_ABI_PROBE_H_
#define MOCKTAIL_JNIVM_ABI_PROBE_H_

#include <string>

// MOCKTAIL_ABI_PROBE=<path> records every JNI class, method and field the
// guest asked Mocktail to resolve, plus which of those methods landed on one of
// Mocktail's own compat handlers. The guest already reveals its own ABI at
// runtime -- ResolveMethodId is asked for a name and signature whether or not
// Mocktail understands it -- so this only has to preserve what it already saw
// instead of discarding it.
//
// Diff two probes, one per Roblox build, to see exactly which names moved. A
// drop in "recognized" is a rename upstream that silently stopped a handler.
namespace jnivm {

// Writes the probe JSON. Does nothing when MOCKTAIL_ABI_PROBE is unset, so
// callers can invoke this unconditionally. Must run before the JNI layer tears
// down its tables, which in practice means straight after the runtime returns.
void WriteAbiProbe();

}  // namespace jnivm

#endif  // MOCKTAIL_JNIVM_ABI_PROBE_H_