---
title: LKM
description: Full flow for installing FinaSU with a loadable kernel module, including partition choice and troubleshooting
createTime: 2026/10/24 00:00:00
permalink: /en/guide/install/lkm/
copyright:
  creation: original
  license: CC-BY-4.0
  author:
    name: LyraVoid
---

# LKM

<Badge text="Recommended" type="tip" /> <Badge text="Modifies the ramdisk" type="warning" />

LKM does not replace the Linux kernel. The patch process unpacks the boot image, puts `ksuinit` and the `kernelsu.ko` matching the device KMI into the ramdisk, and `ksuinit` then loads the driver early in boot. It suits most users and is the simplest persistent option when migrating from another root solution.

::: tip Already Rooted?

If the device is already rooted by Magisk, KernelSU or one of its forks, or by APatch/KernelPatch, do not stack FinaSU on top of the old boot image. Read the matching switching guide first:

- [Switching from Magisk](/en/guide/switch/magisk/)
- [Switching from KernelSU](/en/guide/switch/kernelsu/)
- [Switching from APatch](/en/guide/switch/apatch/)

:::

## How LKM Works

```mermaid
flowchart LR
    A["Stock boot image"] --> B["Patch process unpacks the ramdisk"]
    B --> C["Write ksuinit and kernelsu.ko"]
    C --> D["Flash the patched boot or init_boot"]
    D --> E["ksuinit loads the driver at boot"]
    E --> F["Kernel starts ksud"]
    F --> G["Manager connects and authorizes"]
```

The Linux kernel does not change in this chain. FinaSU only modifies the ramdisk so the device loads one extra module early in boot. LKM risk therefore centers on whether the boot image partition, KMI and slot match.

## Confirm the Partition First

::: table LKM Target Partitions

| Device situation | Modified by default | Restore to stock by flashing |
| --- | --- | --- |
| Has `init_boot` and the KMI is not `android12-*` | `init_boot` | Stock `init_boot` |
| No `init_boot`, or the KMI is `android12-*` | `boot` | Stock `boot` |
| `vendor_boot` selected by hand | `vendor_boot` | Stock `vendor_boot` |

:::

One-line summary: ==LKM changes the ramdisk and usually flashes init_boot, falling back to boot only when there is no init_boot==. Kernels on `android12-*` are the exception: even with `init_boot` present, `boot` is still the default.

::: important Follow What the Manager Shows

Boot layouts differ between devices. The install page shows the default partition it detected; do not guess from the Android version or a tutorial online. A wrong partition writes the image to the wrong place and directly prevents the device from booting.

:::

::: warning Do Not Cross-flash

`boot.img` cannot be flashed into `init_boot`, and `init_boot.img` cannot be flashed into `boot`. On A/B devices, also confirm the current slot; touch only the current slot and keep the other slot bootable.

:::

## Before You Install

:::: field-group

::: field Stock image
@type `Required`
@default `boot / init_boot`
Prepare the stock image that exactly matches the current Android version, kernel version and slot. Do not start without a stock image.
:::

::: field Bootloader
@type `Required`
@default `Unlocked`
Confirm the computer can run `fastboot devices`. Do not use flashing tools of unknown origin.
:::

::: field KMI
@type `Required`
@default `Detected automatically`
The Manager detects the KMI from the device. When you specify it by hand, it must match the current kernel exactly; a nearby version is not a substitute.
:::

::: field Old root
@type `Must be removed`
@default `None`
Magisk, KernelSU or its forks, and APatch/KernelPatch cannot share the boot chain with FinaSU.
:::

::: field Bootloop rescue plan
@type `Required`
@default `Prepared`
Read [Bootloop Rescue](/en/guide/rescue-bootloop/) and make sure you know how to restore the partition the installation actually modifies.
:::

::::

## Choose an Installation Method

::: table LKM Installation Methods

| Method | When to use | Requires existing root |
| --- | --- | --- |
| [Select a file and patch](#select-a-file-and-patch) | First install; handling a downloaded boot image by hand | No |
| [Download a file and patch](#download-a-file-and-patch) | You have a download URL for a complete OTA package or factory image | No |
| [Direct install](#direct-install) | The current device already provides root; patch and flash the current slot directly | Yes |
| [Install to the inactive slot](#install-to-the-inactive-slot) | An A/B device has just finished an OTA but has not rebooted | Yes |

:::

Ordinary users should prefer "Select a file and patch" or "Download a file and patch". When root is already present and the device is GKI, "Direct install" can be used to upgrade.

## Select a File and Patch

For a first install, or for handling a boot image extracted from a complete firmware package by hand.

::: steps

1. **Extract the stock image**

   Extract `boot.img`, `init_boot.img` or another boot image the Manager supports from firmware that exactly matches the current system. Do not use files from another model or another system version.

2. **Choose the patch entry**

   In FinaSU, open **Install & Flash → Select a file and patch** and pick this image.

3. **Confirm the partition**

   Keep the partition the Manager selects by default. Switch it by hand only when you know this device's partition layout exactly; LKM usually targets `init_boot`, or `boot` when there is no `init_boot`.

4. **Choose the driver**

   Prefer the LKM built into the Manager. Select a local `.ko` only when a custom driver is unavoidable, and make sure the architecture, KMI and kernel configuration match exactly.

5. **Patch and export**

   When patching finishes, copy the output image to the computer. Write it to **the same partition and the current slot** with `fastboot` or the flashing method the device supports.

:::

::: code-tabs

@tab:active boot

```sh
fastboot flash boot boot_patched.img
```

@tab init_boot

```sh
fastboot flash init_boot init_boot_patched.img
```

:::

The file names above are examples only. On A/B devices, confirm the current slot first:

```sh
fastboot getvar current-slot
```

If the current slot is `a`, you usually want `boot_a` or `init_boot_a`; if Fastboot already handles slots for you, keep the partition name the Manager shows. Do not use `--slot all` to write both slots at once.

::: danger

When the device will not boot after flashing, stop rebooting it repeatedly and restore the partition the install actually modified, following [Bootloop Rescue](/en/guide/rescue-bootloop/). Continuing to flash guessed images widens the damage.

:::

## Download a File and Patch

Enter the download URL of a complete OTA package or factory image. The Manager checks which boot partitions the package contains, tries to identify the KMI from the kernel part, and then runs the same flow as "Select a file and patch".

Downloading does not adapt the image to your device. You still have to confirm that the device model, system version, partition and A/B slot all match; otherwise a successful patch can still fail to boot.

::: warning The Download Source Must Be Trusted

Use only official firmware or a trusted complete OTA source. After the image downloads, check its version and checksums before handing it to the Manager to patch.

:::

## Direct Install

When the device already has root through another method, you can patch and flash the current slot of the current device directly. This is the most convenient way to upgrade FinaSU, but it still overwrites the boot partition.

Before a direct install, turn on "Back up before flashing" and keep an external stock image as well. A Manager backup is a convenience, not a complete bootloop rescue plan.

::: warning Confirm the Partition Before Installing Over Another Manager

If the old manager's LKM is in `boot`, choose `boot` for FinaSU as well; if the old LKM is in `init_boot`, choose `init_boot`. When unsure, flash back the stock image and patch a file instead of stacking blindly.

:::

::: danger A Magisk-patched Image Cannot Be Used for Direct Install

FinaSU boot image patching rejects a ramdisk already patched by Magisk. On a device using Magisk, first restore the stock image as described in [Switching from Magisk](/en/guide/switch/magisk/).

:::

## Install to the Inactive Slot

This method only applies at the moment an A/B device has finished an OTA but has not rebooted.

The Manager writes the patch result to the other slot and tries to switch to it on the next reboot, so the new system and FinaSU take effect together. Do not use it for an ordinary reboot, after the system has already switched, or when you cannot confirm the versions in both slots.

If the new slot fails to boot, follow [Bootloop Rescue](/en/guide/rescue-bootloop/) to check the current slot and switch back to the old slot that still boots. Do not switch blindly when the old slot has been overwritten, the OTA has already merged, or you cannot confirm the system version in both slots.

::: tip AnyKernel3 Usually Belongs to Built-In Mode

Everyday LKM installs use "Select a file and patch" or "Direct install" and do not need AnyKernel3. If you have an AnyKernel3 ZIP from a kernel author, it is usually for installing a built-in kernel; see [the AnyKernel3 section in Built-In](/en/guide/install/built-in/#what-is-anykernel3) for what it does and the risk.

:::

## Using a Local LKM File

The patch process can use your own `.ko`, but an identical file name does not mean ABI compatibility. The driver must match the device architecture, KMI and kernel configuration at the same time; a kernel module of unknown origin runs with kernel privileges and should not be loaded.

Without a clear reason, use the LKM shipped with the Manager. Before swapping in a local LKM, read the support scope in [Quick Start](/en/guide/get-started/) and prepare a matching boot image recovery plan.

::: table A Custom LKM Must Match

| Item | Requirement |
| --- | --- |
| Architecture | Match the device ABI, for example arm64 |
| KMI | Exactly match the current kernel KMI |
| Kernel configuration | Compatible with the target kernel's symbols, version and configuration |
| Signature trust | The Manager and kernel certificate relationship matches |

:::

## Flash Options

::: table Flash Options

| Option | Effect | Recommendation |
| --- | --- | --- |
| Back up before flashing | Best-effort save of the boot partitions or state the Manager supports | Recommended; does not replace an external stock image |
| Back up as stock image | Marks the current file as a stock backup | Turn on only when the file really comes from official firmware |
| Allow shell | Lets the shell user obtain root | Most users do not need this |
| Enable ADB root | Forces ADB root on and turns authentication off | High risk; leave it off unless debugging |

:::

## Verify After Installation

::: steps

1. **Confirm the run mode**

   The home screen should show **LKM**, not Built-In or Jailbreak mode.

2. **Confirm the Manager identity**

   FinaSU should be able to read the version, authorized apps and managed modules. If it shows as not installed, check the partition, KMI and signature.

3. **Test root**

   Request root from a test app and confirm the authorization dialog and `su` work.

4. **Verify modules**

   Enable one module and reboot, then confirm Folk Mount and the module scripts work.

5. **Verify the built-in engines**

   When using Sui, YukiZygisk or NoMount, check each status; Zygisk and Sui usually need a reboot to finish injection.

:::

## Updates and OTA

The LKM driver version follows the payload embedded in the APK. After updating the Manager, if the home screen reports a newer bundled LKM, you need to patch and flash the boot image again; updating only the APK does not replace the loaded driver. See [Updating FinaSU](/en/guide/update/) for the full flow.

On an A/B device that has finished an OTA but not rebooted, you can choose "Install to the inactive slot". Non-A/B devices must restore the stock image to complete the OTA, then patch the current system's boot image again.

## Data and Recovery

```file-tree title="LKM data locations"
/data/adb
├── ksud                      # userspace command-line tool
├── ksu/
│   ├── .allowlist            # authorization state
│   └── profile/              # App Profile
└── modules/                  # installed modules
```

To recover, flash back the partition the install actually modified:

::: code-tabs

@tab Non-A/B devices

```sh
fastboot flash boot boot.img
fastboot flash init_boot init_boot.img
```

@tab A/B devices

```sh
fastboot getvar current-slot
fastboot flash boot_a boot.img
fastboot flash init_boot_a init_boot.img
```

:::

Run only the command for the partition of your current deployment. If you flash the wrong one, follow [Bootloop Rescue](/en/guide/rescue-bootloop/).

## Troubleshooting

::: collapse accordion

- **It reports `Cannot work with Magisk patched image`**

  The input is still a Magisk-patched image. Restore the stock image first, then patch as described on this page.

- **The device will not boot after flashing**

  Stop rebooting repeatedly and restore the partition the install actually modified, following [Bootloop Rescue](/en/guide/rescue-bootloop/). Check the partition, slot, KMI and image version first.

- **The device has no `init_boot`**

  This is normal. When there is no separate ramdisk partition, LKM uses the ramdisk in `boot`.

- **The Manager shows not installed**

  Check whether the boot image was flashed to the current slot, whether the KMI matches, and whether the kernel trusts the Manager signature.

- **Root works but modules do not mount**

  Check whether the module is enabled, whether the Folk Mount provider works, and whether the module depends on interfaces specific to another root solution.

- **Direct install is unavailable**

  This option requires existing root and a GKI device. When those conditions are not met, use "Select a file and patch".

- **Root disappears after an OTA**

  The system update replaced the boot image. Patch again as described in [Updating FinaSU](/en/guide/update/); on A/B devices, use the inactive slot while the OTA has not yet rebooted.

:::

## FAQ

::: chat title="LKM Installation Q&A"
{User}
Does LKM replace my kernel?

{.}
No. LKM only modifies the ramdisk in the boot image, putting `ksuinit` and `kernelsu.ko` into it; the Linux kernel itself does not change.

{User}
Should I choose boot or init_boot?

{.}
The Manager decides automatically. When the device has `init_boot` and the KMI is not `android12-*`, it is usually `init_boot`; otherwise it is `boot`. The install page is authoritative.

{User}
Can I install directly on a Magisk-patched image?

{.}
No. FinaSU rejects a ramdisk patched by Magisk. First restore the stock image as described in [Switching from Magisk](/en/guide/switch/magisk/).

{User}
What does direct install require?

{.}
Existing root and a GKI device. Without root or on a non-GKI device, use "Select a file and patch".

{User}
Can I swap the local `.ko` freely?

{.}
No. It must match the architecture, KMI, kernel configuration and signature trust relationship. Do not load modules of unknown origin.

{User}
Why is root still the old version after updating the Manager?

{.}
Updating the APK does not automatically replace the LKM in the boot image. Patch and flash again as described in [Updating FinaSU](/en/guide/update/).
:::

## Related Pages

- [Installation Options](/en/guide/install/)
- [Built-In installation](/en/guide/install/built-in/)
- [Jailbreak mode installation](/en/guide/install/jailbreak/)
- [Switching from Magisk](/en/guide/switch/magisk/)
- [Switching from KernelSU](/en/guide/switch/kernelsu/)
- [Switching from APatch](/en/guide/switch/apatch/)
- [Updating FinaSU](/en/guide/update/)
- [Bootloop Rescue](/en/guide/rescue-bootloop/)

*[LKM]: Loadable Kernel Module
*[KMI]: Kernel Module Interface, the Android kernel module interface version
*[GKI]: Generic Kernel Image
*[OTA]: Over-The-Air system update
*[AVB]: Android Verified Boot
