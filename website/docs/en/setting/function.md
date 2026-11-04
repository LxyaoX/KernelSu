---
title: Feature Settings
description: Mount policies, YukiZygisk, Sui, NoMount, kernel info spoofing and network isolation
createTime: 2026/10/24 00:00:00
permalink: /en/setting/function/
copyright:
  creation: original
  license: CC-BY-4.0
  author:
    name: LyraVoid
---

# Feature Settings

<Badge text="Settings entry 4/8" type="info" /> <Badge text="Changes system behavior" type="danger" />

Features is not an ordinary appearance page. It controls how modules are mounted, which kernel information is hidden, whether Zygisk and Sui run, and whether apps can reach the network. Before changing anything, confirm the current deployment method and the kernel's supported range.

::: tip How to get there

Open Manager → Settings → tap the first entry in the second row, **Features**.

:::

## How the Features Relate

```mermaid
flowchart TD
    A["FinaSU kernel driver"] --> B["Root / App Profile"]
    A --> C["Folk Mount picks the mount backend"]
    C --> D["Built-in Magic Mount"]
    C --> E["Metamodule"]
    C --> F["NoMount VFS"]
    A --> G["YukiZygisk"]
    G --> H["Zygisk modules"]
    G --> I["Sui<br/>Shizuku API backend"]
    A --> J["Kernel info spoofing / network isolation"]
```

::: important A tappable switch does not mean the current kernel supports it

Many features map the state returned by the kernel to `supported`, `forced`, `managed` or `unsupported`. A grayed-out control usually means the kernel does not support it, the boot configuration forces it, or a module has taken it over; it does not mean the button is broken.

:::

## Root and Module Visibility

### Unmount Modules (Kernel Level)

This switch lets the kernel hide module-injected content from target apps according to the "Unmount modules" policy in App Profile.

- When on, apps that match the policy see a file system view closer to stock.
- When off, the kernel does not perform this per-app unmount layer.
- The files actually seen still depend on the current Folk Mount backend.

This capability does not share its list with the NoMount exception apps. NoMount has its own visibility model.

### Default Unmount Modules

Sets the global default for "Unmount modules" in App Profile.

- On: apps without their own configuration hide module injection by default.
- Off: apps without their own configuration keep module files by default.
- A single app's App Profile can override the default.

::: tip When an app cannot find module files

Do not disable the whole module mount system first. Check whether that app's App Profile has "Unmount modules" enabled, then check the NoMount exception list to see whether the app needs to see injected content.

:::

### Hide Mount Records

Removes Root and module-related records from the `/proc` mount view of ordinary apps and isolated processes.

The switch appears when available; it shows "unavailable" when the current kernel cannot use it, and returns to the actual state when a write fails. This feature does not delete modules, and it does not replace per-app unmounting.

## Logging and Security Helpers

### AVC Log Spoofing

Handles KernelSU target contexts in SELinux audit logs, reducing the chance that the logs expose related objects directly.

It is not full SELinux hiding, and it does not hide every system difference. The switch cannot be changed when the kernel does not report support.

### SU Log

Records Root execution, legacy SU, authorization and log events. After you turn it off, new SU events are no longer recorded, but existing logs are not deleted by this switch.

For viewing and filtering, see [SU Log](/en/manager/su-log/).

### Allow Any App to Become Dynamic Manager

This is an experimental feature. By default, only a manager with a preset signature can see "Set as dynamic manager" in the app UI; when this is on, any app may show that entry.

::: danger This widens the trust boundary

Do not set unknown apps as dynamic manager just to try it. The entry is hidden completely when the current kernel does not support it.

:::

For the dynamic manager flow, see [Dynamic Manager](/en/manager/dynamic-manager/).

## YukiZygisk

YukiZygisk is the Zygisk engine that ships with FinaSU. Injection happens in the kernel; `ksud` installs the payload, starts the daemons at boot, and writes the settings into `yzconfig.json`.

The switch at the top of the page is the master control (on by default). The status area can show:

- **Running**: the daemon is up and its injection endpoint is registered.
- **Injection is off in the kernel**: the daemon runs, but the kernel's injection bit is clear; tapping the row restarts the engine.
- **Kernel module too old**: this `kernelsu.ko` has no injection runtime, so flash a kernel module from this release.
- **Engine stopped**: the daemon failed to start; the page shows the last line it logged.
- **Not running**: the payload is installed but no process is up.

Changing any setting below writes the configuration and restarts the engine immediately.

::: warning Half of injection lives in the kernel

Installing the manager alone injects nothing: the matching `kernelsu.ko` has to be flashed as well.

:::

## Sui

<Badge text="Requires a Zygisk backend" type="warning" />

Sui is the Shizuku API backend built into FinaSU. Sui is not a Zygisk provider itself; ==it must rely on a working Zygisk backend to run==. You can use the built-in YukiZygisk or an external compatible Zygisk provider; either can serve as Sui's backend.

The practical difference between an external and a built-in provider is this: ==Sui can run, but FinaSU cannot verify the injection result of that external provider==. The status may therefore read "external provider enabled, but injection unverified", which does not mean Sui is necessarily unusable.

The Features page is where you:

- View Sui's current state and the Zygisk backend it depends on.
- Enable or disable Sui.
- Try to repair the payload when it is missing or outdated.
- Open the Sui permission management page.
- View daemon, conflict and payload diagnostics.

A confirmation prompt appears when you enable Sui. If Zygisk is currently unavailable, a conflict exists or the payload is missing, the switch may be inoperable; handle the diagnostic result first.

::: info Full lifecycle

For enabling Sui, payload installation, dual daemons, manager registration, permissions and troubleshooting, see [Sui Guide](/en/builtin/sui/).

:::

## Recommended Troubleshooting Order

:::: steps

1. **Confirm the state first, do not keep toggling**

   Read `forced`, `managed`, `unsupported` or the specific error text first to determine whether the problem is permission, kernel or configuration.

2. **Separate the configured value from this boot's value**

   Folk Mount, Zygisk and Sui have both a "next boot" and a "current run" state. First confirm whether you have rebooted.

3. **Check the dependency chain**

   Sui checks Zygisk; NoMount checks the driver API and KMI; module mounting checks the current Folk Mount provider.

4. **Change one item at a time**

   Do not switch the mount backend, Zygisk, Sui and hiding policies all at once. Reboot and verify item by item so you know which one caused the change.

5. **Return to a safe baseline when needed**

   Set Folk Mount back to "Automatic", turn off experimental features you are not using, then locate the problem from the logs.

::::

## Features Q&A

::: chat title="Feature settings Q&A"
{User}
YukiZygisk shows enabled, so why can't Sui work yet?

{.}
Check the Zygisk injection state for this boot. Enabled is the choice for the next boot; if YukiZygisk stepped aside for an external provider, or injection has not finished, Sui may still be unavailable.

{User}
Can Sui work with an external Zygisk provider?

{.}
Yes. Sui relies on a working Zygisk backend, and both the built-in YukiZygisk and external compatible providers can serve. The only difference is that FinaSU cannot verify the external provider's injection result.

{User}
Why can every app see modules after switching to NoMount?

{.}
NoMount has no exception apps initially. To hide them, select the apps that should see the stock file system under "NoMount → Exception apps", then reboot or apply as the page suggests.

{User}
Does turning Folk Mount off uninstall modules?

{.}
No. Installed modules are not deleted. It only stops injecting module content during this boot, so the modules stay installed but do not take effect.

:::

## Related Pages

- [Root & Mount Policy](/en/setting/root-policy/)
- [YukiZygisk](/en/builtin/yukizygisk/)
- [Sui Guide](/en/builtin/sui/)
- [SUSFS](/en/builtin/susfs/)
- [SU Log](/en/manager/su-log/)
- [Dynamic Manager](/en/manager/dynamic-manager/)
