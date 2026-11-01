#!/bin/bash
set -e

# x86_64 (`kdir-x64`) build of the same module, for running the kernel side on a
# desktop. Only android14-6.1 exists here: the 6.6 / 6.12 / 6.18 targets went away
# with the rest of the multi-series support.
KMI="${1:-android14-6.1}"

if [ "$KMI" != "android14-6.1" ]; then
    echo "only android14-6.1 is supported; got '$KMI'" >&2
    exit 1
fi

CLANG=r487747c

export ARCH=x86_64
export LLVM=1
export LLVM_IAS=1

ORIG_PATH="$PATH"
# Split from the assignment on purpose: `export PATH="$(...)"` masks realpath's
# exit status from shellcheck and from the caller.
CLANG_BIN=$(realpath "/opt/ddk/clang/$CLANG/bin")
export PATH="$CLANG_BIN:$ORIG_PATH"

ODIR="$(realpath .)/out-x64/$KMI"
MDIR="$(realpath .)"
KDIR=/opt/ddk/kdir-x64/$KMI

echo "========== Building $KMI =========="
make -C "$KDIR" "M=$ODIR" "src=$MDIR" compile_commands.json modules \
    CONFIG_KSU=m CONFIG_KSU_X86_PATCH_SYSCALL_DISPATCHER=y

export PATH="$ORIG_PATH"

echo "========== Final output =========="
ls -l out-x64/*/kernelsu.ko
