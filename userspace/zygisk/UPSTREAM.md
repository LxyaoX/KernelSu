# YukiZygisk upstream provenance

YukiZygisk is vendored so that the engine this build ships is built and signed
here, not downloaded from upstream releases: ksud embeds the daemons and the
three libraries, and the injection half lives in `kernel/feature/yukizygisk/`.

Upstream publishes it twice - inside KernelSU at `userspace/zygisk` and
`kernel/feature/yukizygisk`, and as the standalone `Rouyashiki/YukiZygisk`
repository. The KernelSU copy is the one this repository follows, because a
FinaSU tree is laid out the same way: same `uapi/`, same `userspace/ksud` role
and the same list of files under `userspace/zygisk`.

## Pinned source

- KernelSU: https://github.com/Rouyashiki/YukiSU
  commit `c39dfd8be` (branch `main`), which is `a90d1fa9` plus the seven zygisk
  commits below. Imported paths: `userspace/zygisk/**`,
  `kernel/feature/yukizygisk/**`, `uapi/yukizygisk.h`.
- YukiPLT: https://github.com/Rouyashiki/YukiPLT
  commit `f8988f3730cc93ba8121d0e0a3873f320dda7497`.

  | upstream commit | what it changed |
  | --- | --- |
  | `dd32548ba` | replaced the LSPlt PLT hook with YukiPLT and introduced `plt_lifecycle.cpp` / `process_maps.cpp` |
  | `d09348b63` | cached hook registry per Core, managed fork, and the YukiPLT pin bump to `bb58c275` |
  | `485e94b90` | ARM32 only: the `musttail` return in `yuki_bootstrap_tango` now carries its `NOLINTNEXTLINE` |
  | `5c260d1c6` | one lifetime rule for the managed fork and the cached hooks: a fork is published only after the hook registry is sealed, and the registry is retired before the fork is released |
  | `bb215474f` | native companion bindings are published through one atomic hand-off (`native_module_binding.hpp`) instead of being written field by field |
  | `8c122fc7a` | YukiPLT pin to `f8988f37` (Clang formatting, separate/minimised library artifacts, optional Full LTO for Release) |
  | `413d1ea8c` | resident modules keep their linker registration: `hide_injection()` and `prepare_linker()` are gone, so Bionic TLS metadata stays valid for a module that keeps a `PT_TLS` and calls `dl_iterate_phdr` from a worker after specialization |
  | `551083a62` | **not taken**: it fixes `userspace/ksud/src/yzctl.cpp`, which this tree does not have (ksud is Rust and never calls `YZ_RELOAD`) |

  | upstream path | vendored path |
  | --- | --- |
  | `userspace/zygisk/**` | `userspace/zygisk/**` |
  | `kernel/feature/yukizygisk/**` | `kernel/feature/yukizygisk/**` |
  | `uapi/yukizygisk.h` | `uapi/yukizygisk.h` |

Everything imported is byte-identical to upstream apart from the two
deviations below. `kernel/feature/yukizygisk/**` is byte-identical with no
exception, which is also why `kernel/Makefile`'s `check-format` prunes that
directory: reformatting it would turn every re-sync into a large conflict over
whitespace only.

## FinaSU deviations

1. `userspace/zygisk/daemon/CMakeLists.txt` - the daemon's ksud headers come
   from `ksud_compat/`, not `userspace/ksud/src/`. This tree's ksud is Rust, so
   there are no C++ sources there to link; the helpers those headers declare are
   defined by the daemon's own `log.cpp` and by the standalone subset in
   `standalone.cpp`. Linking the ksud copies as well defines them twice.
2. `userspace/zygisk/daemon/zygiskd.cpp` - `nl_listen()` keeps working against
   a kernel module built before `KSU_IOCTL_YZ_GET_EVENT_CHANNEL` existed. Those
   kernels stamped every event with `seq 0`, so a daemon that cannot ask for a
   channel generation leaves its copy at 0 and matches events by type again,
   which is exactly what it did before the check. Upstream instead refuses to
   start ("matching kernel required"); that is the right answer for a build that
   always ships the matching kernel, and the wrong one for a manager that can be
   updated on its own, where a refusal shows up as injection that silently does
   nothing.

`userspace/zygisk/third_party/yukiplt/` is the one path with no upstream file to
compare against: upstream carries it as a git submodule
(`https://github.com/Rouyashiki/YukiPLT.git`) and a submodule does not survive a
tarball sync, so the PLT hooking library is checked in here as plain sources at
`bb58c275`. It replaced `third_party/lsplt/` wholesale; the vendored copy keeps
the library's own layout (`CMakeLists.txt`, `cmake/`, `include/`, `src/`,
`tests/`, `docs/`) because `core/CMakeLists.txt` adds it with
`add_subdirectory(../third_party/yukiplt ...)`.

## Re-syncing

Compare `userspace/zygisk`, `kernel/feature/yukizygisk` and `uapi/yukizygisk.h`
against the pinned commit, re-apply the two deviations, and update the commit
above. `git diff` of the standalone repository is *not* the reference: it has
been refactored to run on its own paths (`/data/adb/yukizygisk/log`, its own
`yzctl`), and none of that applies to a ksud that already stores state under
`/data/adb/ksu/yukizygisk`.

Since the channel now belongs to an active runtime, ksud must enable the kernel
feature before it spawns the daemons - `yukizygisk::post_fs_data`,
`ensure_running` and `set_enabled` all do. A daemon started first finds no
channel, exits, and leaves injection down until the next `services` stage.
