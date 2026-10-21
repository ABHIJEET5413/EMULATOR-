#!/bin/sh
# Apply to a mounted, writable Android system-image root while QEMU is stopped.
set -eu

if [ "$#" -ne 1 ] && [ "$#" -ne 3 ]; then
    echo "Usage: $0 SYSTEM_ROOT [DATA_ROOT HOST_ADB_PUBLIC_KEY]" >&2
    exit 2
fi

system_root=$(realpath "$1")
script_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -d "$system_root/system/etc/init"
test -f "$system_root/system/etc/prop.default"
grep -q '^ro.adb.secure=1$' "$system_root/system/etc/prop.default" || {
    echo 'Expected ro.adb.secure=1; review this image before provisioning.' >&2
    exit 1
}

install -o 0 -g 0 -m 0644 "$script_root/droidbox-adb.rc" "$system_root/system/etc/init/droidbox-adb.rc"
setfattr -n security.selinux -v u:object_r:system_file:s0 "$system_root/system/etc/init/droidbox-adb.rc"

if [ "$#" -eq 3 ]; then
    data_root=$(realpath "$2")
    public_key=$(realpath "$3")
    test -d "$data_root/misc/adb"
    # Never accept a private PEM key or overwrite other authorized hosts.
    grep -q 'PRIVATE KEY' "$public_key" && { echo 'Supply adbkey.pub, never adbkey.' >&2; exit 1; }
    key_file="$data_root/misc/adb/adb_keys"
    touch "$key_file"
    key=$(cat "$public_key")
    grep -Fqx -- "$key" "$key_file" || { printf '\n%s\n' "$key" >> "$key_file"; }
    chown 1000:2000 "$key_file"
    chmod 0640 "$key_file"
    setfattr -n security.selinux -v u:object_r:adb_keys_file:s0 "$key_file"
fi

sync
echo 'TCP ADB startup provisioned. Unmount the images before starting QEMU.'
