---
title: /data/adb Storage Layout
description: Real paths for FinaSU, modules, Lua plugins, kernel modules, Sui, logs, and runtime state
createTime: 2026/10/24 00:00:00
permalink: /en/manager/storage/
copyright:
  creation: original
  license: CC-BY-4.0
  author:
    name: LyraVoid
---

# /data/adb Storage Layout

<Badge text="Diagnostic reference" type="tip" /> <Badge text="Do not delete casually" type="danger" />

FinaSU shares `/data/adb` with other Root components. The directories here hold authorizations, modules, plugins, logs, and runtime state. Deleting a flag file that looks unrelated can change the behavior of the next boot.

::: warning This page is for locating things, not for hand editing

Unless the relevant page explicitly says otherwise, do not modify these files directly. Act through the Manager first, and only read or copy things while troubleshooting.

:::

## Overview

```file-tree title="/data/adb main layout"
/data/adb/
├── ksud                         # ksud executable on the device
├── modules/                     # regular Android modules
│   └── <module-id>/
├── modules_update/              # pending module updates
├── metamodule -> modules/<id>   # current Metamodule symlink
├── plugins/                     # Lua plugins
│   └── <plugin-id>/
└── ksu/
    ├── .allowlist               # kernel Root authorization and App Profiles
    ├── .ksurc                   # ksud runtime config
    ├── bin/
    │   └── ksud -> /data/adb/ksud
    ├── lib/
    │   └── libadbroot.so
    ├── log/                     # Manager, SU and mount logs
    ├── lkm/                     # Linux .ko files imported by the Manager
    ├── module_configs/          # persistent and temporary module config
    ├── plugin_locks/            # Lua plugin locks
    ├── plugin_stage/            # Lua plugin staging
    ├── plugin_state/            # Lua plugin confirmation state
    ├── profile/
    │   ├── templates/           # App Profile templates
    │   └── selinux/             # App Profile SELinux rules
    ├── sui/                     # Sui state and logs
    ├── workdir/                 # built-in mount backend workdir
    ├── mount_mode               # mount provider config for the next round
    ├── mount_runtime            # mount result for this boot
    ├── nomount_runtime          # NoMount result for this boot
    ├── preinit_status           # pre-init status
    └── zygisk.json              # built-in Zygisk config
```

The actual directories vary with the boot method, feature switches, and kernel support. A file that does not appear usually means that feature has not created state yet, not that the installation is damaged.

## Core Files

:::: table FinaSU itself

| Path | Purpose | Caution |
| --- | --- | --- |
| `/data/adb/ksud` | The userspace command line tool. | The Manager and other components call it through subcommands; replacing the version can cause a UAPI mismatch. |
| `/data/adb/ksu/bin/ksud` | A compatibility entry that points at the main `ksud`. | Do not create a replacement link pointing at an unknown binary. |
| `/data/adb/ksu/lib/libadbroot.so` | The library used by the ADB Root feature. | Deleting it breaks ADB Root; it does not remove ordinary Root. |
| `/data/adb/ksu/.allowlist` | The authorization and profile data maintained by the kernel. | A malformed format can lose authorizations or invalidate policy. |
| `/data/adb/ksu/.ksurc` | The `ksud` runtime configuration. | Do not rewrite it casually with a plain text editor. |

::::

## Regular Modules

Regular modules live in `/data/adb/modules/<module-id>/`. Common files inside a module:

:::: table Module flags

| File | Meaning |
| --- | --- |
| `module.prop` | Module ID, name, version, author, and description. |
| `post-fs-data.sh` | Early boot script. |
| `service.sh` | Service stage script. |
| `boot-completed.sh` | Boot completed script. |
| `action.sh` | The manual script run by the Manager "Action" button. |
| `webroot/` | Module WebUI files. |
| `disable` | Disable flag; the module takes no part in later boots. |
| `remove` | Pending removal flag; the directory is removed when the cleanup stage runs after a reboot. |
| `update` | Update flag, meaning the module directory comes from a pending update merge. |
| `skip_mount` | Skips file injection, but module scripts can still run. |

::::

`modules_update/` stages updates, and `metamodule` is a symlink to the current metamodule directory. Do not delete a `remove` flag by hand and keep using a half-updated directory, and do not operate the same module from two managers at once.

::: important `skip_mount` is not disabling

`skip_mount` only makes the mount backend skip the file injection of that module, while the module scripts are still eligible to run. `disable` keeps the module out of the boot entirely. Regular module cards do not currently show `skip_mount` as a normal switch.

:::

## Kernel Modules

`/data/adb/ksu/lkm/` holds the `.ko` entries imported through the Kernel Modules page:

```file-tree title="Managed kernel modules"
/data/adb/ksu/lkm/
├── <managed-id>/
│   ├── module.ko
│   ├── config.json
│   ├── disable
│   └── last_result.json
├── .state/
└── .stage/
```

- `module.ko` is the actual file;
- `config.json` stores automatic loading, parameters, and metadata;
- `disable` is the persistent block on loading;
- `last_result.json` records the most recent load attempt;
- `.state/`, `.stage/`, and the rollback directory are maintained by the Manager.

This is completely different from `/data/adb/modules/`. Do not move files between them just because both are called modules.

## Lua Plugins

Plugins are installed in `/data/adb/plugins/<plugin-id>/`:

| File | Purpose |
| --- | --- |
| `plugin.json` | Plugin manifest and callback declarations. |
| `main.lua` | The default entry; another entry name from the manifest may be used. |
| `config.json` | Plugin configuration saved by the Manager. |
| `last_output.log` | Appended callback output; rewritten from an empty file once the limit is exceeded. |
| `disable` | Disable flag. |

`/data/adb/ksu/plugin_locks/`, `plugin_stage/`, and `plugin_state/` hold install transactions, process locks, and package confirmation state. They are internal runtime data.

## App Profile and Sui

:::: table Profiles and Sui

| Path | Content |
| --- | --- |
| `/data/adb/ksu/profile/templates/` | App Profile templates. |
| `/data/adb/ksu/profile/selinux/` | SELinux rules used by app Root profiles. |
| `/data/adb/modules/finasu-sui/` | The Sui payload module directory. |
| `/data/adb/ksu/sui/` | Sui state, boot markers, manager identity, and daemon logs. |
| `/data/adb/ksu/sui.lock` | The Sui lifecycle operation lock. |

::::

Do not delete the `finasu-sui` directory by hand to "turn off Sui". Sui has its own start, stop, uninstall, and payload management flow; see the [Sui Guide](/en/builtin/sui/).

## Logs

`/data/adb/ksu/log/` may contain:

- `sulog-YYYY-MM-DD*.log`: SU logs;
- `mount.log`: module mount results;
- `mount_capture.log`: an optional capture when a boot does not complete;
- other kernel and built-in engine diagnostic logs.

The log directory usually has strict permissions. When reporting an issue, prefer the export function of the Manager, and first check whether the output contains personal paths, package names, or credentials.

## Mount and Kernel State

These files record what actually happened during the current boot and what the next boot is prepared to use:

| Path | Purpose |
| --- | --- |
| `mount_mode` | The Folk Mount provider selected for the next boot. |
| `mount_runtime` | The mount provider actually used in this boot and its result. |
| `nomount_runtime` | NoMount driver and injection state for this boot. |
| `nomount_uids` | UIDs whose module files should be hidden. |
| `mount_inflight` | Prevents an operation that did not finish during boot from being retried unconditionally on the next boot. |
| `workdir/` | Working directory of the built-in mount backend. |

::: danger State files cannot be used to force things into effect

Editing `mount_mode`, deleting `mount_inflight`, or hand-forging runtime records bypasses safety protections and does not replace the correct settings and reboot flow.

:::

## Backup and Restore Principles

::: steps

1. Use the backup and export features the Manager provides first.
2. When a manual copy is needed, stop related writes and keep file permissions and SELinux contexts.
3. Do not copy all of `/data/adb` over another device.
4. Before restoring, check the device model, system version, Root deployment method, and where the paths came from.
5. After restoring, re-check authorizations, modules, plugins and mount state.

:::

## Related Pages

- [Backup & WebDAV](/en/manager/backup/): what restorable content actually does.
- [Module Management](/en/manager/module/): regular module operations.
- [Lua Plugins](/en/manager/plugin/): plugin directories and lifecycle.
- [Kernel Modules](/en/manager/kernel-module/): `.ko` state and configuration.
- [Core Concepts](/en/guide/information/): background on the module system and system partitions.
