# Build and package

Use Clang, CMake 3.24 or newer, Ninja and the Android NDK. Select the native ABI
of the process that will load the library. The minimum Android API is 23.

```sh
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 \
  -DYUKIPLT_LIBRARY_TYPE=SHARED -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
cmake --install build-android --prefix package --component production
```

This local CMake install contains `lib/libyukiplt.so`, `include/yukiplt.h` (C) and
`include/yukiplt.hpp` (C++), the license and these
integration guides. For the `c++_shared` configuration, deploy the matching NDK's
`libc++_shared.so` too, including for C callers. Android does not supply that
runtime. The `c++_static` shared configuration has no such dependency.

For an existing CMake project, use `add_subdirectory(path/to/YukiPLT)` and link
your target with `yukiplt`. Configure the parent project with the same NDK,
ABI, API and STL settings. Project warnings apply only to YukiPLT's targets.

Release builds use `YUKIPLT_RELEASE_OPTIMIZATION=Oz` and
`YUKIPLT_LTO=ON` by default. Select `Oz`, `O2` or `O3` explicitly to
compare optimization settings for your workload. `YUKIPLT_LTO=ON` requires
Clang and CMake 3.24 or newer; configuration checks an archive-to-host Full LTO
link and fails if the selected toolchain cannot complete it. These options
affect YukiPLT's Release target, leaving other targets and build types unchanged.
CMake-installed static Full LTO packages propagate the required final-link option
through their exported CMake target. Use the matching Clang/LLVM toolchain when consuming a
bitcode archive. Set `YUKIPLT_LTO=OFF` to disable LTO; this also permits
CMake 3.22 or newer. Existing build directories retain their cached setting;
pass `-DYUKIPLT_LTO=ON` to enable it there. Measure sanitizer builds separately
from production builds.

The `Build and check` GitHub Actions workflow runs on pushes, pull requests and
manual dispatch. It uses NDK r30 (`30.0.16248370`) and CMake 4.1.2 to build API 23
Release packages for all four supported ABIs, with Full LTO enabled. Each job
builds one library type, and each artifact contains only that binary at its root:

- `yukiplt-<abi>-shared-libcxx-static`: `libyukiplt.so` with static libc++.
- `yukiplt-<abi>-shared-libcxx-shared`: `libyukiplt.so` with shared libc++.
- `yukiplt-<abi>-static`: `libyukiplt.a`; the final host link selects libc++.

CI disables debug information for all three variants and strips shared
libraries before packaging, checking that their dynamic exports are preserved.
The static archive contains LLVM bitcode and retains the symbols required for
linking; consume it with the matching NDK toolchain and `-flto=full` at final link.
Use headers from the same source revision. For the shared-libcxx-shared variant,
supply that NDK's matching `libc++_shared.so` as described above.

CI checks all tracked C/C++ sources and headers with the NDK's clang-format.
It also runs the NDK's clang-tidy on `src/` using each ABI/STL shared build's
compilation database and the repository's `.clang-tidy` policy. Formatting
violations and clang-tidy errors fail the workflow; tidy warnings remain
warnings. These jobs build and analyze Android code; they do not run it on an
Android device.

Shared artifacts use hidden visibility, an explicit export map, 16K LOAD
alignment and NODELETE. Internal calls bind to the same copy of the library.
Keep the owning loader group alive; NODELETE is not a substitute for the
[lifetime contract](lifecycle.md).

On a 16K device, a 4K-aligned library loaded in page-size compatibility mode
may have an executable GOT mapping. YukiPLT refuses executable, shared or
unreadable slot pages with `PermissionDenied`. Build target libraries with
16K alignment where possible; never bypass this check with writable-executable
permissions. The actual VMA, rather than ELF RELRO flags, decides eligibility.

Select `YUKIPLT_LIBRARY_TYPE=STATIC` for a PIC archive or `SHARED` (the default)
for a shared library. Both are supported Android configurations. The library
type is independent of `ANDROID_STL=c++_static` or `c++_shared`. Static archives
do not contain libc++; the final host link selects its runtime. A shared build
with static libc++ is for C ABI consumers; do not exchange STL objects with a
separate runtime. HWASan requires
arm64-v8a and API 34 or newer.

Installed packages support `find_package(YukiPLT CONFIG REQUIRED)` and
`target_link_libraries(host PRIVATE YukiPLT::yukiplt)`. Set `CMAKE_PREFIX_PATH`
to the install prefix; the prefix may be moved. With the NDK toolchain also
append that prefix to `CMAKE_FIND_ROOT_PATH`, or set `YukiPLT_DIR` directly to
its `lib/cmake/YukiPLT` directory. The target advertises C++17 for C++ callers. Pure C sources can use the C headers without including C++ headers;
when linking the static archive, enable the C++ toolchain and set the final
target's `LINKER_LANGUAGE` to `CXX`.

For source embedding, include `cmake/YukiPLTSources.cmake` and use its
`YUKIPLT_SOURCES` list. Compile implementation sources as C++17 without language
extensions, with PIC, `YUKIPLT_STATIC=1`, hidden default/inline visibility and
the public include directory. Define `YUKIPLT_STATIC` in caller translation
units too. Supply the same runtime and system dependencies as the library
target. Do not also link a second copy of the archive into that host.

Static hosts must localize implementation and archive symbols at final link
and keep the owning code alive through recovery and quiescence. Before unloading,
release all C/C++ library objects, call `yukiplt_c_trim_idle_storage()` to release
idle Registry backing allocations, and drain library operations, callbacks and
replacement calls, including future calls through cached pointers. Trim refuses
unconsumed recovery handles and does not prove unload safety. Compiler/runtime
TLS reclamation is a separate toolchain boundary; end threads that used an
unloadable static runtime before unloading its host. The core
rejects hooks into the entire image that contains its implementation, including
an embedding host. Shared-library hardening flags are not propagated to hosts.

Implementation sources are grouped by responsibility: `elf.cpp` validates ELF
metadata and resolves hashes; `imports.cpp` decodes relocations, versions and
explicit candidates; `memory.cpp` owns live maps, page protections and slot
transactions; `registry.cpp` owns claims and recovery state. `yukiplt.cpp`
coordinates public operations and diagnostics, `session.cpp` discovers images
and manages plans/sessions, and `rules.cpp` manages refresh and loader
notifications. The C boundary has separate operation/token, metadata, session
and rule units. Internal helpers stay with these owners.
