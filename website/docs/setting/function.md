---
title: 功能
description: 挂载策略、YukiZygisk、Sui 与 NoMount
createTime: 2026/10/24 00:00:00
permalink: /setting/function/
copyright:
  creation: original
  license: CC-BY-4.0
  author:
    name: LyraVoid
---

# 功能

<Badge text="设置入口 4/8" type="info" /> <Badge text="会改变系统行为" type="danger" />

“功能”不是普通外观页。这里控制模块怎样挂载、哪些内核信息被隐藏、Zygisk 与 Sui 是否运行，以及应用能否联网。改动前先确认当前部署方式和内核支持范围。

::: tip 进入方式

打开管理器 → 设置 → 点第二行第一个 **功能**。

:::

## 功能之间是什么关系

```mermaid
flowchart TD
    A["FinaSU 内核驱动"] --> B["Root / App Profile"]
    A --> C["Folk Mount 选择挂载后端"]
    C --> D["Built-in Magic Mount"]
    C --> E["Metamodule"]
    C --> F["NoMount VFS"]
    A --> G["YukiZygisk"]
    G --> H["Zygisk 模块"]
    G --> I["Sui<br/>Shizuku API 后端"]
```

::: important 开关可点不等于当前内核支持

很多功能会把内核返回的状态映射成 `supported`、`forced`、`managed` 或 `unsupported`。置灰通常表示内核不支持、启动配置强制或模块接管，而不是按钮坏了。

:::

## Root 与模块可见性

### 卸载模块（内核级）

该开关允许内核根据 App Profile 的“卸载模块”策略，为目标应用隐藏模块注入内容。

- 开启后，符合策略的应用看到更接近原厂的文件系统视图。
- 关闭后，内核不执行这层按应用卸载。
- 实际看到的文件仍受当前 Folk Mount 后端影响。

该能力与 NoMount 的例外应用不是同一套列表。NoMount 有自己独立的可见性模型。

### 默认卸载模块

设置 App Profile 中“卸载模块”的全局默认值。

- 开启：没有单独配置的应用默认隐藏模块注入。
- 关闭：没有单独配置的应用默认保留模块文件。
- 单个应用的 App Profile 可以覆盖默认值。

::: tip 应用找不到模块文件时

不要先关闭整个模块挂载系统。先检查该应用的 App Profile 是否启用了“卸载模块”，再到 NoMount 例外列表中检查是否需要让它看到注入内容。

:::

### 隐藏挂载记录

从普通应用和隔离进程的 `/proc` 挂载视图中移除 Root 与模块相关记录。

可用时会显示开关；当前内核无法使用时会显示“不可用”，写入失败时开关会回到实际状态。该功能不会删除模块，也不会替代按应用卸载。

## 日志与安全辅助

### AVC 日志伪装

处理 SELinux 审计日志中的 KernelSU 目标上下文，降低日志直接暴露相关对象的概率。

它不是完整的 SELinux 隐身，也不会隐藏所有系统差异。内核未报告支持时开关不可修改。

### SU 日志

记录 Root 执行、传统 SU、授权和日志事件。关闭后不再记录新的 SU 事件，但已有日志不会被此开关自动删除。

查看与筛选方法见 [SU 日志](/manager/su-log/)。

## YukiZygisk

YukiZygisk 是随 FinaSU 提供的 Zygisk 引擎。注入由内核完成，`ksud` 负责安装载荷、在开机时拉起守护进程，并把设置写进 `yzconfig.json`。

页面顶部是总开关（默认开启）。状态区可能显示：

- **运行中**：守护进程已就绪，并已注册注入端点。
- **内核注入未开启**：守护进程在运行，但内核的注入开关没有打开，点该行可重启引擎。
- **内核模块版本过旧**：当前 `kernelsu.ko` 不含注入运行时，需要刷入本版本的内核模块。
- **引擎已退出**：守护进程启动失败，页面会显示它写下的最后一行日志。
- **未运行**：载荷已安装，但进程没有启动。

修改下方任一设置会立即写入配置并重启引擎。

::: warning 注入的一半在内核

只更新管理器不会产生注入：必须同时刷入本版本的 `kernelsu.ko`。

:::

## Sui

<Badge text="依赖 Zygisk 后端" type="warning" />

Sui 是 FinaSU 内置的 Shizuku API 后端。Sui 本身不是 Zygisk 提供者，==它必须依赖可用的 Zygisk 后端才能运行==。可以使用内置 YukiZygisk，也可以使用外部兼容的 Zygisk 提供者；两者都能作为 Sui 的后端。

外部提供者与内置提供者的实际差异是：==Sui 可以运行，但 FinaSU 无法验证该外部提供者的注入结果==。因此状态可能显示为“外部提供者已启用，但注入未验证”，这不等于 Sui 一定不可用。

功能页负责：

- 查看 Sui 当前状态和依赖的 Zygisk 后端。
- 启用或关闭 Sui。
- 在载荷缺失或版本陈旧时尝试修复载荷。
- 打开 Sui 权限管理页。
- 查看守护进程、冲突和载荷诊断。

启用 Sui 时会出现确认提示。若当前 Zygisk 不可用、存在冲突或载荷缺失，开关可能不可操作；先按诊断结果处理。

::: info 完整生命周期

Sui 的启用、载荷安装、双守护进程、管理器登记、权限和排错见 [Sui 使用指南](/builtin/sui/)。

:::

## 推荐排查顺序

:::: steps

1. **确认状态，不先反复切换**

   先看 `forced`、`managed`、`unsupported` 或具体错误文本，确认问题是权限、内核还是配置。

2. **区分配置值和本次启动值**

   Folk Mount、Zygisk、Sui 等有“下次启动”和“本次运行”两种状态。先确认是否已经重启。

3. **检查依赖链**

   Sui 检查 Zygisk；NoMount 检查驱动 API 与 KMI；模块挂载检查当前 Folk Mount 提供者。

4. **一次只改一项**

   不要同时切换挂载后端、Zygisk、Sui 和隐藏策略。逐项重启验证，才能知道是哪项造成变化。

5. **需要时回到安全基线**

   把 Folk Mount 切回“自动”，关闭不用的实验功能，再按日志定位。

::::

## 功能问答

::: chat title="功能设置问答"
{用户}
YukiZygisk 显示已开启，为什么 Sui 还不能用？

{.}
检查本次启动的 Zygisk 注入状态。开启是下次启动选择；若 YukiZygisk 让位给外部提供者，或注入尚未完成，Sui 仍可能不可用。

{用户}
外部 Zygisk 提供者能用 Sui 吗？

{.}
可以。Sui 依赖可用的 Zygisk 后端，内置 YukiZygisk 和外部兼容提供者都可以。区别只是 FinaSU 无法验证外部提供者的注入结果。

{用户}
切换到 NoMount 后为什么应用都能看到模块？

{.}
NoMount 初始没有例外应用。需要隐藏时，在“NoMount → 例外应用”中选择应看到原厂文件系统的应用，然后重启或按页面提示应用。

{用户}
关闭 Folk Mount 会卸载模块吗？

{.}
不会删除已安装模块。它只是本次启动不注入模块内容，模块会保持已安装但不生效的状态。

:::

## 相关页面

- [Root 与挂载策略](/setting/root-policy/)
- [YukiZygisk](/builtin/yukizygisk/)
- [Sui 使用指南](/builtin/sui/)
- [SUSFS](/builtin/susfs/)
- [SU 日志](/manager/su-log/)
