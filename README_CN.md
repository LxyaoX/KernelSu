<div align="center">
<img src="logo.png" width="180" alt="FinaSU logo">

[![Latest Release](https://img.shields.io/github/v/release/LyraVoid/FinaSU?label=Release&logo=github)](https://github.com/LyraVoid/FinaSU/releases/latest)
[![Telegram](https://img.shields.io/badge/Telegram-FinaSU-blue?logo=telegram)](https://t.me/FinaSU)
[![License](https://img.shields.io/github/license/LyraVoid/FinaSU?logo=gnu)](/LICENSE)

</div>

**语言：** [English](./README.md) / [中文](./README_CN.md)

FinaSU 是基于 [KernelSU](https://github.com/tiann/KernelSU) 构建的 Android 内核级 Root 方案，集成了内核 `su` 与 Root 权限管理、模块系统、内置 Zygisk 引擎、SUSFS 支持以及可配置的现代化管理器。

---

## 功能概览

FinaSU 提供：

- 内核级 `su` 与基于白名单的 Root 权限管理
- 基于 overlayfs 与 metamodule 的类 Magisk 模块系统
- App Profile，将 Root 权限限制在单个应用策略内
- 内置 YukiZygisk 引擎（内核辅助注入），提供总开关与引擎设置
- SUSFS 支持，按内核能力开关
- 内核级隐藏：挂载隐藏、SELinux 隐藏、AVC 日志伪装与卸载策略
- `.fpt` 主题，与 FolkPatch 互通
- 多页壁纸与自定义字体
- 模块仓库浏览与一键安装
- 应用内语言切换与社区翻译

## 前置要求

- arm64-v8a 的 Android 设备，且内核为 **android14-6.1**（Linux 6.1，Android 14 世代）。本树只发布这一个内核系列：模块对其它系列直接拒绝编译，ksud 只内嵌 `android14-6.1_kernelsu.ko`，管理器也会如实说明，而不是照样给一个装不上的入口。
- 通过 LKM（可加载的 `kernelsu.ko`）、修补 boot 镜像或 AnyKernel3 包进行安装

## 管理器

- 多种首页布局，包括 GridUI 与仪表盘式页面
- 可配置的主题：配色、壁纸与字体
- 分类式设置中心，涵盖通用、外观、行为、功能、安全与备份
- 可选更新检查与开机模块屏蔽

## 模块与扩展

### KernelSU 模块

标准 KernelSU 模块由管理器管理。模块通过内置的 NoMount VFS 后端注入，不再需要 metamodule；已安装的 metamodule 仍会被识别并列出。

### YukiZygisk

YukiZygisk 是内置的 Zygisk 引擎。注入由 `kernelsu.ko` 内的内核特性完成；`ksud` 负责安装载荷、开机拉起守护进程，并让 `/data/adb/ksu/yukizygisk/yzconfig.json` 与管理器保持一致。运行时在内核里，所以只更新管理器不会产生注入——必须刷入同版本的内核模块。

### SUSFS

SUSFS 管理按能力开关。仅在支持的内核上显示相关控件，每次配置变更都会附带应用回执。

### Folk-Mount

Folk-Mount 是管理器对模块挂载后端的称呼，只有两种设置：NoMount（VFS 驱动，默认）与关闭。

### NoMount

NoMount 是内建的 VFS 路径重定向后端：不产生任何挂载，而是由内核模块直接接管路径解析与目录遍历，因此不会污染 `/proc/mounts`。内核模块随 ksud 一起分发（按 KMI 内嵌，由本仓库自行编译），后端本身不依赖模块或 `nm` 二进制。

管理器提供驱动状态、注入规则数、可见性例外与「关闭挂载」；挂载模式只有 `NoMount`（默认）与 `关闭`。它后面已经没有第二个引擎：驱动装载不起来时，那次开机就不注入任何模块，设置页会直接说明，而不会换一种方式挂载。ksud 还会在装载与注入之前先落一份「进行中」记录：如果某次 NoMount 注入之后没能走完启动，下次开机会直接跳过注入，并把原因写进 `/data/adb/ksu/log/mount.log`；同一份日志（有体积上限、会自动轮转）也会记下每次注入的规则数与命中的顶层目录，方便从 bug 报告里判断注入到底碰了哪些路径。要查"卡住时内核在等什么"，可以先 `ksud mount capture on`（默认卡过 150 秒就取证，`ksud mount capture on 60` 可以改时长，`off` 关掉）：取证结果同样落在 `/data/adb/ksu/log/mount_capture.log`，包含 `sysrq w` 打印的全部阻塞任务栈、内核日志结尾、D 状态任务与 pstore。可见性是一份由 ksud 保存的例外名单：名单内的应用看到原厂文件系统，其余应用都能看到注入的文件；NoMount 页可以按需把各应用 Profile 的「卸载模块」判定复制进这份名单，改动 Profile 后需要再复制一次。

目录型符号链接会被当作目录递归遍历并**逐文件合并**（带环路检测与按模块去重）；`.replace` 标记或 opaque 目录同样默认降级为逐文件合并，只有白名单里的 `/product/fonts`、且其原目录不存在或为空时才允许整目录替换。分区根目录，以及名为 `lib64`、`lib`、`bin`、`etc` 的目录，**永远不会**被整目录替换。降级会写进日志：`mount.log` 里带 `dir_replace=N`，`ksud nomount inject --dry-run --verbose` 也会分别标注文件重定向与目录替换。

### TEESimulator

TEESimulator 为选定的应用回应密钥认证请求：它把 AOSP 的参考 KeyMint 实现跑在进程内，因此返回的证书链是真实设备会产出的那一份，而不是自己拼的证书。载荷由管理器自己携带：打开开关后会把运行时部署到 `/data/adb/teesim/runtime`，把 `sepolicy.rule` 交给 `ksud sepolicy apply`，再由 `post-fs-data.d` 钩子拉起守护进程 —— 模块列表里不会多出条目，首次启用也不需要重启。认证链的成色取决于你导入的 keybox：没有 keybox 时拦截器保持不生效，所有密钥照旧走真实硬件。运行时的 logd 读取库不会被部署，所以 `/data/adb/teesim/log` 下不会再产生日志文件；管理器页面是唯一的配置入口。

## 安全提示

管理器通过 APK 签名验证自身身份：只有受信任的管理器以及用户显式认领的应用才能管理内核。Root 权限基于白名单，只有被授权的应用才能访问 `su`。漏洞上报请参阅 [SECURITY.md](./SECURITY.md)。

## 翻译

英语和中文是参考语言。翻译修正请只提交对应语言的文件；新增语言时，请在 Pull Request 中提供完整翻译文件。

## 下载与安装

1. 从[发布页面](https://github.com/LyraVoid/FinaSU/releases/latest)下载最新安装包。
2. 在设备上安装管理器。
3. 按应用内引导安装内核模块、修补 boot 镜像，或刷入 AnyKernel3 包。

## 从源码构建

仓库提供了与 CI 一致的本地构建入口：

```bash
./build.sh ksud     # 构建 Rust 用户态守护进程
./build.sh lkm      # 构建内核模块
./build.sh apk      # 构建管理器 APK（需先构建 ksud）
./build.sh all      # 完整本地构建
```

各组件具体的构建与检查命令请参阅 [AGENTS.md](./AGENTS.md)。

## 开源致谢

- [KernelSU](https://github.com/tiann/KernelSU)：上游项目
- [YukiSU](https://github.com/Anatdx/YukiSU)：YukiZygisk 的内核特性与用户态载荷
- [YukiSU](https://github.com/Anatdx/YukiSU)：动态管理器参考
- [BakaSU](https://github.com/Baka-SU/BakaSU)：AnyKernel3 刷写及部分小功能参考
- [Magisk](https://github.com/topjohnwu/Magisk)：`magiskpolicy` 与模块概念
- [SUSFS](https://gitlab.com/simonpunk/susfs4ksu)：内核级隐藏
- [FolkPatch](https://github.com/LyraVoid/FolkPatch)：主题格式与设计参考
- [MMRL](https://github.com/MMRLApp/MMRL)：模块仓库格式参考
- [小赖字体](https://github.com/lxgw/kose-font)：内置应用字体（Copyright 2020-2024 LXGW）。以子集形式分发：GB2312 + ASCII + 常用标点，因为完整字体要占管理器 APK 的 8.2MB；集合外的字会回落到系统字体。

## 许可证

- `kernel` 目录下的文件遵循 [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)。
- 其余部分遵循 [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html)。

## 社区

- Telegram：[**@FinaSU**](https://t.me/FinaSU)
