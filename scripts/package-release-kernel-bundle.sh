#!/bin/sh
set -eu

source_dir=${1:-.}
output=${2:-}
if [ -z "$output" ]; then
    echo "usage: $0 <artifact-dir> <output.zip>" >&2
    exit 2
fi

case "$output" in
    /*) ;;
    *) output="$PWD/$output" ;;
esac

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# This fork builds one KMI (android14-6.1) for arm64 only, so the bundle carries a
# single architecture and a single kernel module.
mkdir -p "$stage/arm64"
set -- "$source_dir/aarch64"-android*-lkm/*_kernelsu.ko

for file do
    if [ ! -f "$file" ]; then
        echo "missing aarch64 kernel module artifacts" >&2
        exit 1
    fi
done
cp "$@" "$stage/arm64/"

mkdir -p "$stage/ksud"
if [ -f "$source_dir/ksud-aarch64-linux-android/ksud" ]; then
    cp "$source_dir/ksud-aarch64-linux-android/ksud" "$stage/ksud/android-arm64"
else
    echo "missing ksud-aarch64-linux-android/ksud" >&2
    exit 1
fi

kernelsu=$(find "$stage/arm64" -maxdepth 1 -name '*_kernelsu.ko' | wc -l)
if [ "$kernelsu" -ne 1 ]; then
    echo "arm64 has $kernelsu kernelsu.ko; expected 1 (android14-6.1)" >&2
    exit 1
fi

ksud_count=$(find "$stage/ksud" -maxdepth 1 -type f | wc -l)
if [ "$ksud_count" -ne 1 ]; then
    echo "ksud has $ksud_count binaries; expected 1" >&2
    exit 1
fi

rm -f "$output"
(cd "$stage" && python3 -m zipfile -c "$output" arm64 ksud)
echo "created $output"
