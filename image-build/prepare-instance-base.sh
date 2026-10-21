#!/bin/sh
# Run as root in Linux/WSL with qemu-utils, attr, and ext4 support.
# Create a NEW provisioned base; never mount or modify the supplied base.
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "Usage: $0 ORIGINAL_BASE NEW_BASE [LOCAL_ADB_PUBLIC_KEY]" >&2
    exit 2
fi
[ "$(id -u)" = 0 ] || { echo 'Run as root (WSL: -u root).' >&2; exit 1; }
original=$(realpath "$1")
output=$(realpath -m "$2")
[ ! -e "$output" ] || { echo 'Output already exists; refusing to overwrite it.' >&2; exit 1; }
script_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec 9>/tmp/droidbox-image-preparation.lock
flock -n 9 || { echo 'Another image preparation is running.' >&2; exit 1; }
key=''
if [ "$#" -eq 3 ]; then
    key=$(realpath "$3")
    grep -q 'PRIVATE KEY' "$key" && { echo 'Supply adbkey.pub, never a private key.' >&2; exit 1; }
fi
work=$(mktemp -d "$(dirname "$output")/.droidbox-base.XXXXXX")
nbd=''
cleanup() {
    if mountpoint -q "$work/system" && ! umount "$work/system"; then
        echo "System mount is busy; retaining recovery directory $work" >&2; return
    fi
    if mountpoint -q "$work/disk" && ! umount "$work/disk"; then
        echo "Disk mount is busy; retaining recovery directory $work" >&2; return
    fi
    [ -z "$nbd" ] || qemu-nbd --disconnect "$nbd" >/dev/null 2>&1 || true
    # work is a directory created by this process, never a caller-supplied path.
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 1' INT TERM
modprobe nbd max_part=8
for candidate in /sys/class/block/nbd*; do
    [ -e "$candidate/pid" ] && continue
    nbd="/dev/$(basename "$candidate")"; break
done
[ -n "$nbd" ] || { echo 'No unused NBD device available.' >&2; exit 1; }
qemu-img create -f qcow2 -F qcow2 -b "$original" "$work/work.qcow2"
qemu-nbd --connect="$nbd" "$work/work.qcow2"
sleep 2
mkdir "$work/disk" "$work/system"
mount "${nbd}p1" "$work/disk"
guest=''
for candidate in "$work"/disk/android-*/system.img; do
    [ -f "$candidate" ] || continue
    [ -z "$guest" ] || { echo 'Multiple Android systems found; select the image manually.' >&2; exit 1; }
    guest=$(dirname "$candidate")
done
[ -n "$guest" ] || { echo 'Expected an installed Android-x86 system.img in partition 1.' >&2; exit 1; }
mount -o loop "$guest/system.img" "$work/system"
if [ -n "$key" ]; then
    sh "$script_root/provision-adb-image.sh" "$work/system" "$guest/data" "$key"
else
    sh "$script_root/provision-adb-image.sh" "$work/system"
fi
umount "$work/system"
umount "$work/disk"
qemu-nbd --disconnect "$nbd"
nbd=''
qemu-img convert -O qcow2 "$work/work.qcow2" "$work/base.qcow2"
qemu-img check "$work/base.qcow2"
# Publish only the complete, detached image. Output is on the same filesystem.
ln "$work/base.qcow2" "$output" # Atomic no-overwrite publication on this filesystem.
echo "Prepared base: $output"
if [ -n "$key" ]; then
    echo 'This LOCAL DEVELOPMENT base authorizes the supplied public key; do not distribute it.'
fi
