#!/system/bin/sh
# Invoked by init after boot, as Android's shell UID. Never authorize host keys.
set -eu

# TCP adbd still uses the framework's USB-debugging authorization manager.
# Keep ro.adb.secure=1; Android must ask before trusting a new host key.
settings put global adb_enabled 1

# Configure each fresh instance once. Subsequent boots preserve the user's
# launcher choice. This marker belongs to the writable overlay, not the base.
marker=/data/local/tmp/droidbox-first-boot-v1
if [ ! -f "$marker" ]; then
    settings put global device_provisioned 1
    settings --user 0 put secure user_setup_complete 1
    cmd package set-home-activity --user 0 com.android.launcher3/com.bliss.launcher.BlissLauncher
    touch "$marker"
    chmod 0600 "$marker"
    am start --user 0 -a android.intent.action.MAIN -c android.intent.category.HOME
fi
