# Managed kernel modules

`ksud kmod` manages ordinary Linux kernel modules (`.ko`) that the user installs
explicitly. These entries are separate from Android modules and are stored under
`/data/adb/ksu/lkm/`.

## Commands

All list, info, log, and mutation commands print one JSON object to stdout. Logs
and diagnostics use stderr. A failed command exits non-zero.

```text
ksud kmod list
ksud kmod info <id>
ksud kmod install <path>
ksud kmod remove <id>
ksud kmod enable <id>
ksud kmod disable <id>
ksud kmod autoload <id> <on|off>
ksud kmod params <id> <params>
ksud kmod load <id>
ksud kmod log <id>
```

Examples:

```sh
ksud kmod install /data/local/tmp/example.ko
ksud kmod params example "mode=1 debug=0"
ksud kmod load example
ksud kmod autoload example on
```

Parameters are passed to the module loader as one NUL-terminated string. They
are never evaluated by a shell.

## State

The following states are independent:

- `autoload` is the user's next-boot preference.
- `disabled` is a persistent block. `enable` clears only this block.
- `loaded` is read from `/sys/module/<name>` and is never inferred from an old
  command result.
- `effective_autoload` is true only when `autoload` is true and `disabled` is
  false.

Turning autoload off does not unload a running module. Disabling a module does
not unload it. Removing an entry does not unload an instance that is already in
the kernel.

The UI uses distinct labels for these independent operations:

- `Load now` loads into the current kernel without changing persistent intent.
- `Block future loads` writes the persistent disable marker without unloading.
- `Allow future loads` clears that marker without loading the module.
- The switch controls only the load-on-boot preference.

## Loading

Manual load and autoload use the same `ksuinit::load_module_with_resolver` path
as `ksud insmod`. On supported kernels, unresolved symbols are queried through
KernelSU and fall back to `/proc/kallsyms`; the loader may also perform its
existing vermagic compatibility retry. The managed path does not change SELinux
or module-signing policy.

The internal module name comes from `.modinfo` `name=`. If that field is missing
or ambiguous, the entry remains visible and removable, but load and autoload are
rejected because a reliable runtime state cannot be established.

The built-in `kernelsu` identity is protected from managed installation,
loading, and autoload.

## Startup and safe mode

Autoload runs during `post-fs-data`, after safety checks and before regular
module stage scripts. Startup waits up to 8 seconds for the scheduler; if that
wait expires, the scheduler continues in the background and the timeout does
not cancel or roll back an in-progress kernel load. Each entry is attempted at
most once per boot. A record left in `inflight` state is converted to
`interrupted` on the next boot, and that state keeps blocking automatic retries
until the user performs a manual load attempt.

Safe mode writes a persistent `disable` marker for every managed entry without
parsing its configuration. Import and removal remain available; load, enable,
and enabling autoload are rejected. Leaving safe mode does not clear disable
markers automatically.

Kernel modules execute with kernel privilege. A userspace timeout cannot undo
code that has already entered the kernel, and safe mode cannot recover from a
kernel panic.

## Storage

```text
/data/adb/ksu/lkm/
  <id>/
    module.ko
    config.json
    disable
    last_result.json
  .state/
    registry.lock
    locks/
  .stage/
  .rollback/
```

Installs use a staging directory and an atomic publish step. Updates preserve
the autoload preference, stored parameters, persistent disable marker and
last result. Scans ignore dot directories and symbolic links.
