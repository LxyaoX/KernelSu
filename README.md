<div align="center">
<img src="logo.png" width="180" alt="FinaSU logo">

[![Latest Release](https://img.shields.io/github/v/release/LyraVoid/FinaSU?label=Release&logo=github)](https://github.com/LyraVoid/FinaSU/releases/latest)
[![Telegram](https://img.shields.io/badge/Telegram-FinaSU-blue?logo=telegram)](https://t.me/FinaSU)
[![License](https://img.shields.io/github/license/LyraVoid/FinaSU?logo=gnu)](/LICENSE)

</div>

**Language:** [English](./README.md) / [中文](./README_CN.md)

FinaSU is a kernel-based root solution for Android, built on [KernelSU](https://github.com/tiann/KernelSU). It combines kernel-level `su` and root access management with a module system, a built-in Zygisk engine, SUSFS support, and a configurable modern manager.

---

## Overview

FinaSU provides:

- Kernel-based `su` with whitelist-based root access management
- A Magisk-like module system based on overlayfs and metamodules
- App Profile, to confine root power to a per-app policy
- The built-in YukiZygisk engine (kernel-assisted injection), with a master switch and per-engine settings
- SUSFS support, gated by kernel capability
- Kernel-level hiding: mount hiding, SELinux hiding, AVC log spoofing, and unmount policy
- `.fpt` themes with FolkPatch-compatible exchange
- Multi-page wallpapers and custom fonts
- Module repository browsing and one-tap installation
- An in-app language picker with community translations

## Requirements

- An arm64-v8a Android device on **android14-6.1** (Linux 6.1, the Android 14 generation). It is the one kernel series this tree ships: the module refuses to build for anything else, ksud embeds a single `android14-6.1_kernelsu.ko`, and the manager says so instead of offering an install.
- Installation via LKM (a loadable `kernelsu.ko`), a patched boot image, or an AnyKernel3 package

## Manager

- Multiple home layouts, including GridUI and dashboard-style pages
- Configurable theming: colors, wallpapers, and fonts
- A category-based settings hub covering general, appearance, behavior, function, security, and backup options
- Optional update checks and boot-time module blocking

## Modules and Extensions

### KernelSU modules

Standard KernelSU modules are managed from the manager. Publishing them into the filesystem is the job of a metamodule you install; the manager recognises it and lists it with the rest.

### YukiZygisk

YukiZygisk is the built-in Zygisk engine. Injection happens in the kernel feature that ships in `kernelsu.ko`; `ksud` installs the payload, starts the daemons at boot, and keeps `/data/adb/ksu/yukizygisk/yzconfig.json` in sync with the manager. Because the runtime is kernel-side, updating the manager alone injects nothing - the matching kernel module has to be flashed.

### SUSFS

SUSFS management is capability-gated. The controls appear only on kernels that support it, and every configuration change is applied with a receipt.

### Module mounting

Module mounting is delegated to the metamodule you install (NoMount, meta-overlayfs, ...). This
build ships no mount engine of its own: the built-in NoMount driver, its rule planner and the
mount-mode switch are gone, so a metamodule's own `metamount.sh` is the only thing that publishes
the module tree at boot. Without one, modules stay inert and `ksud` says so in the log instead of
falling back to a second engine.

### TEESimulator

TEESimulator answers key-attestation requests for the apps you select from AOSP's reference KeyMint implementation running in-process, so the chain it returns is the one a real device would produce instead of a hand-rolled certificate. FinaSU ships the payload inside the manager: the switch stages it under `/data/adb/teesim/runtime`, hands its `sepolicy.rule` to `ksud sepolicy apply` and starts the daemon from a `post-fs-data.d` hook, so no module appears in the list and the first enable needs no reboot. Attestation is only as good as the keybox you import - with none, the interceptor stays inert and every key keeps going to the real hardware. The runtime's native logd reader is not deployed, so nothing is written to `/data/adb/teesim/log`; the manager page is the only configuration surface.

## Security

The manager authenticates itself by APK signature: only the trusted manager and apps the user has explicitly claimed can manage the kernel. Root access is whitelist-based, so only granted apps can reach `su`. See [SECURITY.md](./SECURITY.md) for vulnerability reporting.

## Translation

English and Chinese are the reference languages. Only submit corrections for the target language, and include the entire translated file when adding a new language.

## Download and Installation

1. Download the latest package from the [Releases page](https://github.com/LyraVoid/FinaSU/releases/latest).
2. Install the manager on the device.
3. Follow the in-app flow to install the kernel module, patch a boot image, or flash an AnyKernel3 package.

## Building from Source

The repository ships a local mirror of the CI pipeline:

```bash
./build.sh ksud     # build the Rust userspace daemon
./build.sh lkm      # build the kernel module
./build.sh apk      # build the manager APK (requires the ksud build)
./build.sh all      # full local run
```

See [AGENTS.md](./AGENTS.md) for the per-component build and check commands.

## Credits

- [KernelSU](https://github.com/tiann/KernelSU) - upstream project
- [YukiSU](https://github.com/Anatdx/YukiSU) - YukiZygisk kernel feature and userspace payload
- [YukiSU](https://github.com/Anatdx/YukiSU) - dynamic manager reference
- [BakaSU](https://github.com/Baka-SU/BakaSU) - AnyKernel3 flashing and smaller-feature reference
- [Magisk](https://github.com/topjohnwu/Magisk) - `magiskpolicy` and module concepts
- [SUSFS](https://gitlab.com/simonpunk/susfs4ksu) - kernel-level hiding
- [FolkPatch](https://github.com/LyraVoid/FolkPatch) - theme format and design reference
- [MMRL](https://github.com/MMRLApp/MMRL) - module repository format reference
- [Xiaolai](https://github.com/lxgw/kose-font) - the bundled app font (Copyright 2020-2024 LXGW). It ships as a subset: GB2312 plus ASCII and common punctuation, because the full font is 8.2MB of a manager APK. A character outside that set renders in the system font instead.

## License

- Files under the `kernel` directory are [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- All other parts are [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html).

## Community

- Telegram: [@FinaSU](https://t.me/FinaSU)
