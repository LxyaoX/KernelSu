---
title: Built-In Capabilities Overview
description: Entry points, dependencies, timing and troubleshooting for FinaSU built-in engines and kernel features
createTime: 2026/10/25 00:00:00
permalink: /en/builtin/
copyright:
  creation: original
  license: CC-BY-4.0
  author:
    name: LyraVoid
---

# Built-In Capabilities Overview

<Badge text="Enable as Needed" type="info" /> <Badge text="Most Require a Reboot" type="warning" /> <Badge text="Settings Entry" type="tip" />

"Built-in capabilities" means features released with FinaSU whose runtime support is provided by `ksud` and the kernel driver. They are not ordinary modules that must be installed separately; the entry points are gathered under **Settings → Features** in the Manager, but each capability differs in dependencies, timing and risk.

::: warning "Built-in capabilities" is not the Built-In installation

The word "built-in" here describes that the feature ships with FinaSU. It is not the same concept as the [Built-In kernel build option](/en/guide/install/built-in/) in the installation article. An LKM installation can also use Folk Mount, YukiZygisk and Sui.

:::

## I Want To

::: table Find a Capability by Goal

| Goal | Use | Check First | When It Takes Effect |
| --- | --- | --- | --- |
| Run Zygisk modules | YukiZygisk | Whether the kernel module ships the injection runtime | Next boot |
| Give apps that support the Shizuku API Root or Shell | [Sui](/en/builtin/sui/) | FinaSU driver, Zygisk backend, Sui payload | Usually a reboot after enabling |
| Use existing SUSFS hiding capabilities | [SUSFS](/en/builtin/susfs/) | Kernel source integration or a loaded SUSFS-LKM | Depends on the current implementation |

:::

## How They Work Together

These capabilities are not a linear feature list. How module files become visible is decided by the metamodule you install, since FinaSU no longer ships a mount engine; YukiZygisk injects code into app processes with kernel help; Sui then uses Zygisk to provide the Shizuku API backend. SUSFS depends on kernel source integration or a loaded SUSFS-LKM.

```mermaid
flowchart TD
    K["FinaSU kernel driver"] --> Z["YukiZygisk"]
    MM["Metamodule you install"] --> NM["module tree publication"]

    Z --> ZM["Zygisk modules"]
    Z --> S["Sui"]
    Z -.-> X["External compatible Zygisk provider"]
    X -.-> S

    E["SUSFS source patch or LKM"] -.-> O["SUSFS integration"]
    O -. Takes over uname .-> U
```

Solid lines in the diagram mean FinaSU provides or drives the capability directly; dashed lines mean an external provider. Sui can use the built-in YukiZygisk, and it can also use an external compatible Zygisk provider. The real difference with an external provider is that ==Sui can run, but FinaSU cannot verify its injection result==.

## Three Kinds of Capabilities, Do Not Mix Them Up

### Userspace Engines

**YukiZygisk and Sui** have their lifecycle managed by `ksud`, with configuration written to `/data/adb/ksu/`. Whether they actually run during a boot depends on that boot's record, not on the switch alone.

- YukiZygisk may stand down at boot in favor of an external Zygisk provider.
- Sui depends on a working Zygisk backend and is not a Zygisk provider itself.
- Module mounting is done by the metamodule's own `metamount.sh`; without one, modules stay inert and the manager offers no mount mode.

### Kernel Features


### Kernel Patches and LKMs

**SUSFS** is not implemented in this repository. FinaSU only provides state detection, configuration and safe integration. Without a kernel patch you can use a matching SUSFS-LKM, but its feature scope is usually smaller than the source-integrated version.

## Switches, Configuration and This Boot

:::: field-group

::: field Next Boot Configuration
@type `Persistent setting`
For example the Folk Mount mode, the YukiZygisk switch and the Sui enable intent. After being written to disk they still require restarting the relevant component or the device.
:::

::: field This Boot State
@type `Run result`
Indicates which provider this boot actually selected, whether injection completed, or whether the driver is ready. It can differ from the configuration because of external providers, boot protection or driver load failure.
:::

::: field Live Rules
@type `Hot update`
NoMount module rules and exceptions can usually be updated within the current boot. Existing processes may still keep the old state.
:::

::: field Takes Effect for New Processes
@type `Process level`
:::

::::

::: important Separate Who Provides from What Is Remembered

The configuration page stores the desired state. External modules, boot protection, an unavailable driver and a previous abnormal exit can all change the actual provider for this boot. When troubleshooting, check **configuration mode**, **this boot** and **errors/warnings** together.

:::

## Recommended Enable Order

:::: steps

1. **Confirm Root works first**

   Open the Manager, confirm the driver is connected, and run one controlled Root operation successfully. Built-in capabilities cannot repair a broken FinaSU driver.

2. **Enable one capability at a time**

   Finish the lower layer of the dependency chain first: for example confirm that Zygisk actually injects before enabling Sui. Do not change the Folk Mount backend, the Zygisk provider and app policies at the same time.

3. **Check whether a reboot is needed**

   YukiZygisk, Sui and Folk Mount modes usually need a reboot to enter the new state.

4. **Verify with the target app**

   Go back to the target app and check the actual effect. Zygisk modules and Sui permissions should both be judged by the result in the target process, not just the global switch.

5. **Return to a minimal configuration on failure**

   Disable the newly added capability first, confirm that basic Root and boot work, then locate the dependency from the error message. Do not stack switches one after another to "try it out".

::::

## Common Status Terms

| Display or Status | Usual Meaning | Do Not Mistake It For |
| --- | --- | --- |
| **Disabled** | The current configuration does not require this capability to work | The feature is definitely unavailable |
| **Configured** | The setting is saved but has not taken effect yet | The target process has adopted the setting |
| **Mounting** | The boot stage is still running | It has completed successfully |
| **Injected / Mounted / Ready** | This boot passed the corresponding checks | Every app scenario is definitely compatible |
| **Partial** | Only some ABIs, targets or steps succeeded | A total failure or a total success |
| **Unsupported** | The current kernel lacks a feature or the driver does not respond | The Manager button is broken |
| **Taken Over by an External Provider** | Another module or kernel feature is in charge | FinaSU's configuration must have been modified |

Different features use different verification standards. For example YukiZygisk "Injected" requires the current boot, the corresponding ABI, the daemon and the Zygote process to all be confirmed; for Sui, an external provider state may only verify that the service exists, not the full injection chain.

## Data and Diagnostics

```file-tree title="/data/adb/ksu built-in capability locations"
/data/adb/ksu
├── mount_mode                  # Folk Mount next-boot mode
├── mount_runtime               # provider and result for this boot
├── mount_log                   # Folk Mount runtime log
├── zygisk.json                 # YukiZygisk next-boot switch
├── zygisk-boot.json            # boot protection and failure count
├── .zygisk/state.json          # current injection state
├── sui/                        # Sui state and daemon records
├── nomount_runtime             # last NoMount injection result
├── nomount_uids                # NoMount exception UIDs
└── folksu-susfs/               # SUSFS integration configuration and state
```

These files are for diagnostics, not manual configuration entry points. Editing them directly can bypass validation, locks and transaction rollback, causing the Manager display to differ from the kernel's actual state.

## Troubleshooting Order

:::: steps

1. **Check the dependency**

   For Sui check Zygisk first; for NoMount check the driver and mount mode first; 

2. **Check the boot generation**

   Confirm that the "this boot" information on the page belongs to the current boot. When the configuration was changed without a reboot, the two are expected to differ.

3. **Check protection and fallback**

   YukiZygisk pauses attempts after three consecutive boot failures; Folk Mount falls back conservatively after an exception in the NoMount or Metamodule stage. Handle warnings first instead of triggering them again.

4. **Verify with the target process**

   Check whether the target app was restarted, whether its ABI is supported, whether the UID was chosen correctly, and whether an independent namespace or extra permission restriction applies.

5. **Keep logs before restoring**

   When a rollback is needed, record the error text, time, UID/PID and module list first, then disable the corresponding capability. Deleting the data directory loses the evidence used to locate the problem.

::::

## Capability Boundaries

- **Built-in capabilities are not a hiding guarantee.** NoMount and SUSFS each cover only part of the observable surface.
- **Sui is not unconditional Root.** Apps still need to connect to Sui and obtain per-app permission.
- **External Zygisk providers are not managed by FinaSU.** FinaSU can stand down and detect the service, but it will not verify or repair the external provider's injection chain for you.

## Continue Reading

::: table Capability Details

| Capability | Key Content |
| --- | --- |
| [Sui](/en/builtin/sui/) | Payload, boot lifecycle, Manager identity, permissions, background notifications and full troubleshooting |
| [SUSFS](/en/builtin/susfs/) | Kernel source integration, SUSFS-LKM, feature gaps and usage boundaries |

:::

:::: collapse accordion

### Why is the switch on but the feature has no effect?

Check the dependency and this boot's state first. YukiZygisk, Sui and Folk Mount modes all need a new boot cycle.

### Can several built-in capabilities be used at the same time?

Yes, but keep their responsibilities from conflicting. Folk Mount and NoMount are mutually exclusive mount provider choices; Sui clearly depends on Zygisk.

### Do I need to uninstall modules to turn off a built-in capability?

No. Use the switch in the Manager or the corresponding `ksud` lifecycle command. Deleting the state directory manually can leave daemons still running or a state inconsistent with the module disable marker.

### Why does the documentation advise against installing another provider of the same kind?

Two Zygisk providers, two mount executers or two Sui instances will compete for the same injection point, mount path or Binder service. FinaSU can stand down on its own in some scenarios, but it will not resolve the conflict for an external implementation.

::::

:::: chat title="Built-In Capabilities Quick Q&A"
{User}
I just want to run one Zygisk module. Do I need to enable Sui first?

{Assistant}
No. Sui is only a Shizuku API service that uses the Zygisk backend; it is not a prerequisite for all Zygisk modules.

{User}
NoMount shows ready. Does that mean the modules are definitely injected?

{Assistant}
No. ready only means the driver API responds; you also need to look at the expected and actual rule counts and the mount provider of the current boot.

{User}
Can SUSFS be added by installing a module?

{Assistant}
The source-integrated version cannot. You can also load SUSFS-LKM as an alternative, but it is limited by the KMI and exported capabilities, so it usually offers fewer features than the source-integrated version.

::::
