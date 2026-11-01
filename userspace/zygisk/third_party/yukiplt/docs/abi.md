# ABI compatibility

YukiPLT is unreleased. Rebuild all C++ callers with matching public headers,
compiler, NDK and standard-library configuration. Private members affect
by-value object storage; mixing layouts can overwrite caller memory.

The current `HookPlan`, `FunctionHookPlan`, `HookSession`,
`FunctionHookSession` and `RuleSet` layouts are frozen. New capabilities use
new types or entry points. Do not mix STL iterator/debug modes, and do not
infer compatibility from equal `sizeof` alone.

C ABI version 1, existing numeric values and existing C structures remain
unchanged. Use `yukiplt_c_has_feature()` to detect additions. `struct_size`
validates a frozen layout; it does not permit fields to be appended in place.
The legacy recovery diagnostic is frozen even though it has no size field.
`OperationDiagnostic`'s version is an output identifier, not an extension slot.

Compile replacement code using `OriginalSlot::load()`, `load_original()` or
`yukiplt_c_load_original()` so original reads include the required fence.
Path-bound APIs do not remove the caller's lifecycle responsibility for a
same-path, same-address reload.

Shared artifacts bind internal API calls to their own implementations. For a
manually embedded static copy, define `YUKIPLT_STATIC` in every library and
caller translation unit, compile implementation sources with hidden visibility
(`-fvisibility=hidden -fvisibility-inlines-hidden`), and localize archives at the
host DSO link (`-Wl,--exclude-libs,ALL` or a restrictive version script). Export
only your wrapper API. The macro alone does not hide archive implementation
symbols or keep the host alive.

The documented `ANDROID_STL=c++_shared` build depends on the matching NDK's
`libc++_shared.so`, even for C-only callers. The `c++_static` shared variant is for C ABI boundaries. For static embedding,
select the runtime at the final host link. Do not mix incompatible C++ runtime
instances, artifacts or callers.

Use `<yukiplt.h>` for C and `<yukiplt.hpp>` for C++. The former
`yukiplt/` include paths have been removed; update source includes when
rebuilding. Header consolidation preserves existing layouts, numeric values
and exported signatures.
