# FinaSU Agent Guide

## Agent Quick Start

- For significant features or refactors, sketch a plan first; keep it updated as you work.
- Use Context7 to pull library/API docs when you touch unfamiliar crates, Android APIs, or JS deps.
- Default to `rg` for searching and keep edits ASCII unless the file already uses non-ASCII.
- Run the component-specific checks below before handing work off; do not skip failing steps.
- When unsure which path to take, favor minimal risk changes that keep kernel/userspace contracts intact.

## Project Overview

FinaSU is a KernelSU-based root solution for Android with a kernel module, Rust userspace daemons, a
Kotlin Manager app, and docs/web assets. Unlike upstream KernelSU, engines that upstream expects
users to install as modules are vendored into this repository and driven by ksud: YukiZygisk
(Zygisk). Module mounting is not: that is left to a metamodule the user installs.

## Repository Structure

```bash
/kernel/                      # Kernel module - C code for Linux kernel integration
/userspace/ksud/              # Userspace daemon - Rust binary, hosts the built-in engines
/userspace/ksuinit/           # Statically linked init helper that ksud embeds (boot-patch)
/userspace/zygisk/            # YukiZygisk engine (core + daemons, C++ + CMake, vendored)
/manager/                     # Android manager app - Kotlin/Jetpack Compose UI
/website/                     # Documentation website - VitePress
/js/                          # JavaScript library for module WebUI
/.github/workflows/           # CI/CD workflows for building and testing
/scripts/                     # Build and device-test automation scripts
/docs/                        # Release checklist and API notes
```

`/kernel/` is organised by concern rather than as a flat directory:

```bash
/kernel/core/                 # init.c - module entry and boot registration
/kernel/feature/              # sucompat, kernel_umount, selinux_hide, avc_spoof, module_blacklist, adb_root, shell_su_adb_only
/kernel/hook/                 # syscall/lsm hook manager plus per-arch patch_memory and syscall trampolines
/kernel/infra/                # file wrappers, event queue, seccomp cache, symbol resolver
/kernel/manager/              # apk signature checks, package observer, throne tracker
/kernel/policy/               # allowlist, app profiles, feature toggles
/kernel/runtime/              # boot events and ksud integration
/kernel/selinux/              # sepolicy loading and rules
/kernel/sulog/                # su event log ring buffer
/kernel/supercall/            # supercall driver install, dispatch and permission checks
/kernel/include/uapi/         # kernel <-> userspace ABI headers
```

## Core Concepts

- **supercall**: Kernel-side IOCTL interface installed on the `[ksu_driver]` anon-inode by the reboot kprobe hook in `kernel/supercall/supercall.c`, dispatched in `kernel/supercall/dispatch.c` and gated by `kernel/supercall/perm.c`. It maps commands like allowlist/app-profile management, feature toggles, and sepolicy changes. The ABI lives in `kernel/include/uapi/supercall.h` and is mirrored in `userspace/ksud/src/ksu_uapi.rs`; Rust userspace reaches it through `userspace/ksud/src/ksucalls.rs` (scans or installs the FD, wraps IOCTLs), while the Manager JNI bridge mirrors the same IOCTLs in `manager/app/src/main/cpp/ksu.cc` (entry points in `jni.cc`).
- **module**: A flashable ZIP unpacked by `userspace/ksud/src/module.rs` into `/data/adb/modules/` (path constants in `userspace/ksud/src/defs.rs`), with lifecycle scripts (`post-fs-data.sh`, `service.sh`, etc.) executed by ksud init events (`userspace/ksud/src/init_event.rs`). The Android Manager surfaces module state from ksud in `manager/app/src/main/java/me/weishu/kernelsu/ui/viewmodel/ModuleViewModel.kt`.
- **metamodule**: A module marked `metamodule=1` in its own `module.prop`; no in-tree module ships one. ksud recognises the marker in `userspace/ksud/src/metamodule.rs`, enforces a single active metamodule and creates the `/data/adb/metamodule -> /data/adb/modules/<id>` symlink; a metamodule is the only mount backend there is (its `metamount.sh` is run through `module_mount.rs`), and its scripts run before regular modules in `userspace/ksud/src/init_event.rs`. The Manager UI highlights metamodules and warns on uninstall (`manager/app/src/main/java/me/weishu/kernelsu/ui/screen/module/ModuleScreen.kt`).
- **app profile**: Per-app policy struct defined in `kernel/include/uapi/app_profile.h`, validated and persisted in `kernel/policy/app_profile.c` alongside the allowlist in `kernel/policy/allowlist.c`, to control root grants and non-root behavior (e.g., cumulative umount policy). supercall IOCTLs `KSU_IOCTL_GET/SET_APP_PROFILE` are dispatched from `kernel/supercall/dispatch.c` and are consumed by ksud/Manager via the JNI bridge (`manager/app/src/main/cpp/ksu.cc`) and Kotlin model `Natives.Profile` (`manager/app/src/main/java/me/weishu/kernelsu/Natives.kt`).
- **sucompat**: Exec/FS compatibility layer that reroutes `/system/bin/su` to ksud for allowed UIDs, keeping legacy “call su to root” flows working. The hooks live in `kernel/feature/sucompat.c` and are registered by `kernel/hook/syscall_hook_manager.c`; feature toggle `KSU_FEATURE_SU_COMPAT` is declared in `kernel/include/uapi/feature.h` and dispatched in `kernel/policy/feature.c`.
- **allowlist**: Kernel-managed list of UIDs permitted for root, persisted at `/data/adb/ksu/.allowlist` with default root/non-root profiles. Core logic is in `kernel/policy/allowlist.c` (bitmap storage, persistence, default profile caching) and is initialized from `kernel/core/init.c`; the supercall handlers under `kernel/supercall/` expose getters, deny-list checks, and “should umount modules” decisions. Manager reads and edits it through the JNI calls in `manager/app/src/main/cpp/ksu.cc` and Kotlin `Natives` façade.
- **built-in engines**: Code that upstream KernelSU expects users to install as modules, vendored here and driven by ksud. Their user-visible contracts are part of this repository's own API surface, and vendored sources must stay byte-identical to upstream unless the change is recorded in the matching `UPSTREAM.md`:
  - **YukiZygisk** (`kernel/feature/yukizygisk/` + `userspace/zygisk/`) - injection is a kernel feature; `userspace/ksud/src/yukizygisk.rs` installs the payload into `/data/adb/ksu/lib/yukizygisk`, starts the daemons and drives `/data/adb/ksu/yukizygisk/yzconfig.json`. Its ioctls are dispatched by `kernel/supercall/dispatch.c`; the engine does nothing on a kernel module built without the feature.
  - **SUSFS** - `userspace/ksud/src/susfs/` configures an existing kernel-side SUSFS installation only; this repository neither patches nor implements SUSFS.
  - **Module mounting** - there is no in-tree mount engine. `userspace/ksud/src/module_mount.rs` only runs the installed metamodule's `metamount.sh` once per boot; with no metamodule, modules stay inert and the log says so. The built-in NoMount driver, its rule planner, the `nomount`/`mount` commands and the mode switch were removed, along with the manager's mount pages and settings.
  - **Lua plugins** - `userspace/ksud/src/plugin/`.
  - **TEESimulator** (`manager/app/src/main/assets/teesim/`) - the exception: its payload is carried by the manager, not ksud. `data/teesim/TeesimInstaller.kt` unpacks `teesim-arm64.zip.xz` (a zip in an XZ stream - the payload is native code, so LZMA saves a third over deflate) into `/data/adb/teesim/runtime`, applies `sepolicy.rule` through `ksud sepolicy apply`, writes `/data/adb/post-fs-data.d/neosu_teesim.sh` and keeps the daemon alive from `/data/adb/teesim/run.sh`. The archive holds only what runs: upstream minus the x86_64 tree, the WebUI (and its self-tests), the module wrapper and `arm64-v8a/libteesim_logcat.so` - without that library LogTail disables itself, so no thread tails logd and nothing lands in `/data/adb/teesim/log`. `NOTICE.txt` beside it records the pinned canary. After unpacking a new upstream build, strip the payload before re-packing: upstream ships full DWARF, and `arm64-v8a/libteesim_keymint.so` alone carries 18 MB of `.debug_*` that the loader never maps (`llvm-objcopy --strip-unneeded` on every file under `arm64-v8a/` takes the archive from 30.7 MB to 6.8 MB, and the XZ stream from 6.2 MB to 2.0 MB). Compare `readelf -l` before and after - the program headers must stay byte-identical. The targets screen also edits upstream's own `autoIncludeNewApps` on the `default` profile, so apps installed after the simulator first ran are served without naming them; only one profile may carry that flag, so a profile that already holds it is named in the UI instead of being overwritten, and an empty app list is written only while the flag is on because the daemon rejects a profile that targets nothing.

## Build & Packaging (`build.sh`)

`./build.sh` is the local mirror of the GitHub Actions pipeline and the canonical way to produce a flashable release. Do not hand-assemble an APK; use it.

```bash
cp manager/sign.example.properties manager/keystore.properties  # fill in first
./build.sh all   # lkm -> ksuinit -> ksud -> apk -> repack
```

Steps (usable individually, but keep the order above):

- `lkm` - build `kernelsu.ko` for each `KMIS` in the DDK Docker image, then stage it into `userspace/ksud/bin/aarch64/` for ksud to embed.
- `ksuinit` - cross-compile the statically linked `ksuinit` helper ksud embeds (boot-patch fails without it).
- `ksud` - cross-compile the daemon with cargo-ndk. Only `userspace/ksud/bin/aarch64/` is populated: the prebuilt `busybox`/`waitsys`/`bootctl` for x86_64 and riscv64 were dropped, so building ksud for any other target means staging that target's busybox first (`RustEmbed` fails on a missing folder).
- `apk` - `./gradlew :app:assembleRelease`, signed from `manager/keystore.properties`.
- `repack` - inject the freshly built ksud into the APK and re-sign it, writing the deliverables to `dist/`.

This is the proper packaging path: `repack` produces the real release APK with the current ksud baked in. Copying `libksud.so` into `jniLibs` by hand is only a quick debug shortcut, not a release build.

Key constraints:

- `lkm` needs Docker (DDK images `ghcr.io/ylarod/ddk-min:<kmi>-<DDK_RELEASE>`); `ksud`/`ksuinit` need cargo + the Android NDK; `apk` needs a JDK.
- Keep the NDK at r29 (`NDK_VERSION=29.0.14206865`); newer NDKs break the static ksuinit link.
- One KMI only: `android14-6.1`. `KMIS` exists but nothing else is supported - `kernel/include/util.h`
  `#error`s on any other series, ksud embeds only `android14-6.1_kernelsu.ko`, and `boot-patch`
  refuses the rest. A local `lkm` run for another KMI produces a module no code path can load.
- Signing comes from `manager/keystore.properties`; `build.sh` also derives the second trusted manager cert size/sha256 and bakes it into the kernel build.
- Overrides via env: `KMIS`, `ABIS`, `DDK_RELEASE`, `ANDROID_API_LEVEL`, `KSUD_BUILD_TYPE`, `APP_BUILD_TYPE`, `OUT_DIR`, `KEYSTORE_PROPS` (see `./build.sh --help`).

## Component Workflows

### Kernel (`kernel/`)

- The module targets **android14-6.1** only (Linux 6.1, the Android 14 generation). `kernel/include/util.h`
  `#error`s outside 6.1.x in both directions, so the 5.10/5.15 arms below it and the 6.6/6.12/6.18 arms
  above it are dead: if you port code from elsewhere, drop its other-series branches too.
- Kernel changes are C-only; keep interfaces aligned with supercall and allowlist expectations in userspace/Manager.
- If you alter IOCTLs or profiles, update the corresponding wrappers in ksud (`ksucalls.rs`) and Manager JNI (`manager/app/src/main/cpp/ksu.cc`).

### Userspace Rust (`userspace/ksud`, `userspace/ksuinit`)

`userspace/ksud` and `userspace/ksuinit` are the two members of the root cargo workspace. For either
of them, ALWAYS run these commands in sequence after making code changes:

1. `cargo ndk -t arm64-v8a check` (verify compilation)
2. `cargo ndk -t arm64-v8a clippy` (lints and warnings)
3. `cargo fmt` (format)
4. Fix any errors or warnings before considering the task complete.

### Vendored engine (`userspace/zygisk`)

These are not cargo crates. Build them through their own entry points so the vendored sources stay
byte-identical to upstream:

- `userspace/zygisk` - C/C++ engine sources (core + daemons) built by CMake with the NDK; CI compiles both ABIs and stages the eight files under `userspace/ksud/bin/<abi>/yukizygisk/` for ksud to embed.

### Android Manager App (`manager/`)

For a full, signed, installable APK use `./build.sh apk repack` (see Build & Packaging above). For a quick iteration build:

```bash
cd manager
# Must have ksud binaries first!
mkdir -p app/src/main/jniLibs/arm64-v8a
cp ../userspace/ksud/target/aarch64-linux-android/release/ksud app/src/main/jniLibs/arm64-v8a/libksud.so

# Then build
./gradlew clean assembleRelease
```

Important: Manager build REQUIRES ksud binaries to be present in `jniLibs` before building.

`res/font/xiaolai.ttf` is a subset, not the upstream font: GB2312 plus ASCII and common punctuation, 7.5k of the original 17.5k glyphs and 2.9MB instead of 8.2MB (5.4MB of the release APK). Regenerate it with `fonttools` and `subset.Options(hinting=False, name_IDs=["*"], name_languages=["*"], recalc_bounds=True, layout_features=["kern","liga","calt","ccmp","locl","mark","mkmk"])`; the copyright and family records are deliberately kept, and a character outside the set falls back to the system font instead of a missing glyph.

### Website (`website/`)

The committed lockfile is `website/yarn.lock` and CI installs with `--frozen-lockfile`, so use yarn:

```bash
cd website
yarn install --frozen-lockfile
yarn docs:build  # Production build
```

### JavaScript Web UI (`js/`)

- JS packages back module WebUI pieces; follow existing package manager lockfile and run the relevant lint/test scripts before publishing changes.

## Common Pitfalls

- Only one metamodule can be active; keep meta hooks in sync with ksud expectations.
- Manager JNI mirrors every supercall; kernel or ksud API changes must be reflected there to avoid runtime drift.
- Do not skip the `cargo ndk` steps; plain `cargo check` will not validate Android targets.
- Manager builds fail if `libksud.so` is missing; create it before any Gradle command.
- For a release APK use `./build.sh all` (or at least `./build.sh ksud apk repack`) rather than shipping a hand-built `assembleRelease` output.
- YukiZygisk's injection half lives in the kernel: an APK build alone injects nothing, and a kernel module without the feature reports "kernel module too old" on its page.
- SUSFS controls only work on kernels that already carry the kernel-side SUSFS patch; never describe the module as providing SUSFS.
- Module mounting is not built here: there is no NoMount module and no mount engine in ksud any more, so `./build.sh` produces one kernel module and nothing else to embed. A device without a metamodule sees no injected modules, by design.

## Git Commit

- Mirror existing history style: `<scope>: <summary>` with a short lowercase scope tied to the touched area (e.g., `kernel`, `ksud`, `manager`, `yukizygisk`, `docs`, `scripts`). Keep the summary concise, sentence case, and avoid trailing period.
- Prefer one scope; if multiple areas change, pick the primary one rather than chaining scopes. For doc-only changes use `docs:`; for multi-lang string updates use `translations:` if that matches log history.
- Keep subject lines brief (target ≤72 chars), no body unless necessary. If referencing a PR/issue, append `(#1234)` at the end as seen in history.
- Before committing, glance at recent `git log --oneline` to stay consistent with current prefixes and capitalization used in this repo.
- When a commit comes from somewhere else - a merge of another repository, a cherry-pick, an upstream patch - keep the people who wrote it on the commit. Add one trailer per original author, taken from the source commit:
  ```
  Co-authored-by: Name <email>
  ```
  (`git log <range> --format='%an <%ae>' | sort -u` gives the list for a merge; `git log -1 --format='%an <%ae>' <sha>` for a single pick.) Cherry-picks also take `-x`, so the source sha is recorded. A merge commit that swallows five upstream commits credits nobody on GitHub otherwise, and this repository lives on upstream work.
