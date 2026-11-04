---
title: 访问控制
createTime: 2026/10/24 00:00:00
permalink: /setting/access/
copyright:
  creation: original
  license: CC-BY-4.0
  author:
    name: LyraVoid
---

# 访问控制

## ADB Root

以 Root 权限运行 `adbd`。该能力可能要求重启 ADB 服务，并受系统构建与 SELinux 限制。使用后应按需关闭。

## Shell 仅可通过 ADB 获取 Root

对已授权的 Shell 增加来源检查：只有由 `adbd` 或 KernelSU 系 Root 进程启动的 Shell 才能提权。它用于阻止普通应用先提权到 Shell，再借助 Shell 获得 Root。

## 建议

- ADB Root 只在调试期间开启。
- 保留官方管理器签名，不要用相同包名重新签名分发。
