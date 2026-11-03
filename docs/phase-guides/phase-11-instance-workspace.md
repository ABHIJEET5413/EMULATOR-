# Phase 11 — Multi-Instance Workspace and Resource Policies

## Outcome

Sabi’s launcher is now an instance workspace. It shows each local qcow2
overlay’s state, labels, resource values, disk use, base-image health, and
available host storage. It does not pretend to manage instances owned by a
different Sabi process.

## Workspace capabilities

- Search by instance name or local labels; filter All/Running/Stopped; sort by
  name, status, or overlay size.
- Create, start, stop, configure, clone, rename, export backup, reset, and
  delete instances.
- Rename, export, clone, reset, and delete are stopped-instance operations.
  They keep the pre-existing registry and disk lock protections.
- Export copies `config.json`, `overlay.qcow2`, and a README into a newly
  created directory beneath a user-selected destination. It is a portable
  backup artifact, not an automatic import mechanism; it retains the original
  backing-image reference and must be preserved with that base.
- Labels are limited to eight non-empty 32-character local labels.
- **Start stopped** affects available stopped instances. **Stop owned**,
  pause/resume, Grid, and Cascade affect only windows started from this
  workspace process. A lock held by another Sabi process is reported instead
  of being bypassed.

## Resource policy contract

The workspace has only two honest next-start profiles:

- **Balanced:** up to two CPU cores and up to 2048 MB RAM, bounded by Sabi’s
  current host limits.
- **Low resource:** one CPU core and 1024 MB RAM.

These settings are written through `InstanceManager` and become QEMU launch
limits on the next start. They are not an FPS limiter, priority booster,
audio mute, or measured “eco mode.”

Pause/Resume is available only for a current workspace-owned QEMU process and
uses the existing QMP `stop`/`cont` commands. No synchronized input or remote
instance control is added.

## Safety and recovery

- A missing backing image is reported with a non-destructive recovery message.
- Less than 1 GB available host storage produces a warning before operations
  likely to grow an overlay.
- Rename keeps the per-instance lock while releasing the Windows file probe
  immediately before moving the directory; if the new config cannot be
  published, it moves the directory back and restores the original config.
- Destructive reset/delete continue to require confirmation.

## Automated verification

`test_instance_workspace` validates search/filter/sort and health summaries.
The established `test_instance_manager` now validates labels, profiles,
stopped-instance export, rename rollback behavior, reset, and deletion against
real temporary qcow2 files.

## Manual acceptance checklist

- [ ] Create three instances, add labels, search by label, and change every
  filter/sort option using keyboard and mouse.
- [ ] Attempt rename/export/clone/reset/delete while an instance is running;
  confirm Sabi refuses and preserves its overlay.
- [ ] Rename a stopped instance; start it and confirm its Android data remains.
- [ ] Export a stopped instance to an empty folder; confirm it contains config,
  overlay, and README, and that the original instance remains unchanged.
- [ ] Apply Balanced and Low resource profiles; restart the instance and verify
  Settings shows the expected CPU/RAM values.
- [ ] Start two instances from the workspace. Test Pause/Resume, Stop owned,
  Grid, and Cascade. Confirm a separately launched Sabi process is not moved,
  paused, or stopped.
- [ ] Temporarily make a backing image unavailable only on a disposable test
  instance; verify the health message is clear and no disk is deleted.
