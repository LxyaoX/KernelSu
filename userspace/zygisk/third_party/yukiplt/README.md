# YukiPLT

A standalone Android Native PLT/GOT hook library, independently implemented
under Apache-2.0. It requires C++17 and Android API 23 or newer, with no root,
JNI, Java, daemon or private linker dependency.

| ABI | Support | Native relocations |
| --- | --- | --- |
| `arm64-v8a` | Official, guaranteed | ELF64 RELA, APS2 RELA |
| `armeabi-v7a` | Official, guaranteed; low-SDK runtime verification pending | ELF32 REL, APS2 REL |
| `x86_64` | Official, environment-dependent | ELF64 RELA, APS2 RELA |
| `x86` | Official, environment-dependent | ELF32 REL, APS2 REL |

Each artifact handles only its process's native ABI. Other architectures and
API 21–22 are unsupported. Page size is detected at runtime; shared artifacts
use 16K LOAD alignment.

Select images and imports explicitly. YukiPLT provides unique-slot hooks,
version selectors, batch transactions, owning import snapshots, multi-image
sessions, rule registration and synchronous custom-loader notifications.
Function `GLOB_DAT` and APS2 require explicit source/type selection. There is
no automatic loader tracking, inline hook or general hook-chain manager.

- [Build and package](docs/building.md)
- [Integrate the C++ and C APIs](docs/integration.md)
- [Manage lifetime, recovery and loader cooperation](docs/lifecycle.md)
- [Match ABI and runtime dependencies](docs/abi.md)

Read the lifetime contract before installing a hook. Imports must already be
bound, output storage and replacement code must remain alive through recovery,
and every modifying result must be checked. Successful unhook does not prove
that replacement code is no longer executing.

This library is unreleased. Rebuild all C++ callers with matching headers,
toolchain and STL configuration. C ABI v1 retains its fixed layouts and values.

Licensed under [Apache-2.0](LICENSE).
