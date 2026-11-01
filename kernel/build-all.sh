#!/bin/bash
set -e

# This tree supports one GKI series. A module built for another one is refused by
# ksud (it embeds `android14-6.1_kernelsu.ko` only) and by late-load, so the
# default list is a single entry; the argument stays for the old call sites.
KMIS="${1:-android14-6.1}"

mv .ddk-version .ddk-version.bak 2> /dev/null || true

for kmi in $KMIS; do
    echo "========== Building $kmi =========="
    ODIR="$(realpath .)/out/$kmi"
    if ddk build "$kmi" "ODIR=$ODIR" -e CONFIG_KSU=m; then
        if [ -f "$ODIR/kernelsu.ko" ]; then
            cp "$ODIR/kernelsu.ko" "kernelsu-${kmi}.ko"
            llvm-strip -d "kernelsu-${kmi}.ko"
            echo "✓ Built kernelsu-${kmi}.ko"
        fi
    else
        echo "✗ Build failed for $kmi"
    fi
    echo ""
done

mv .ddk-version.bak .ddk-version 2> /dev/null || true

echo "========== Final output =========="
ls -l kernelsu-*.ko
