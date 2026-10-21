# Source inventory

The offline payload contains pinned archives for these installed components:

| Component | Exact source |
|---|---|
| QEMU Windows ar7 fork, 11.1.0 | `e470268ff446ec56728215a987ed166d5ed8fb64`, https://repo.or.cz/qemu/ar7.git |
| Qt Base 6.12.0 | `025bdad181de241e81bf853c8a2d7bf3d19261a9`, https://github.com/qt/qtbase |
| Qt SVG 6.12.0 | `883a58532078d003cefef0a549bfe9f885ce41f5`, https://github.com/qt/qtsvg |

`sources/source-lock.json` records archive hashes. Qt's installed SBOMs are
copied to `licenses/upstream`. Local DroidBox and Android startup modifications
are copied into `sources/droidbox`. Build/deployment instructions are in the
source repository's `installer/BUILD.md`.

Additional projects present in the guest/runtime include:

- Bliss OS 14.10.3: https://blissos.org and https://github.com/BlissOS.
- Linux kernel and Android-x86/AOSP components: retain the exact build's source
  references and notices, rather than treating the aggregate image as Apache-only.
- Neo Store 1.0.5: https://github.com/NeoApplications/Neo-Store.
- microG GmsCore/Companion 0.3.3.240913 / 0.3.3.40226:
  https://github.com/microg/GmsCore.
- microG GsfProxy v0.1.0: https://github.com/microg/GsfProxy.
- Android platform-tools 37.0.1: https://android.googlesource.com/platform/packages/modules/adb.
- QEMU's firmware, submodules and DLL dependencies: identify matching packages
  using the included Windows builder script, binary hashes, and upstream notices.

These project links identify origins; they do not replace complete corresponding
source obligations. A distribution build requires a reviewed release inventory
and additional source artifacts, not merely these three primary archives.
See `SOURCE-OFFER.txt` for source requests and release responsibilities.
