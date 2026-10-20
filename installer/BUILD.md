# Installer Build Instructions

> How to build `DroidBoxSetup.exe` from the compiled application
> and bundled dependencies.

---

## Prerequisites

1. **DroidBox.exe** built in Release mode
2. **Inno Setup 6.x** installed: https://jrsoftware.org/isinfo.php
3. **Qt deployment** done (`windeployqt`)
4. **Bundle components** collected (see below)

---

## Collecting Bundle Components

### 1. Build DroidBox

```powershell
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.5.3\msvc2019_64"
cmake --build build --config Release
```

### 2. Deploy Qt DLLs

```powershell
C:\Qt\6.5.3\msvc2019_64\bin\windeployqt.exe `
    --release --no-translations --no-opengl-sw `
    build\Release\DroidBox.exe
```

### 3. Prepare Bundle Directory

```
installer/bundle/
├── qemu/           ← Extract QEMU for Windows here
│   ├── qemu-system-x86_64.exe
│   ├── qemu-img.exe
│   ├── *.dll
│   └── share/      ← QEMU firmware files
├── adb/            ← Extract Android platform-tools here
│   ├── adb.exe
│   └── *.dll
├── images/
│   └── bliss-base.qcow2  ← Built from image-build pipeline
├── configs/
│   └── default-instance.json
├── keymaps/
│   ├── default.json
│   └── wasd-gaming.json
└── vcredist/
    └── vc_redist.x64.exe  ← From Microsoft
```

### Where to Get Each Component

| Component | Download URL |
|---|---|
| QEMU for Windows | https://qemu.weilnetz.de/w64/ |
| Android platform-tools | https://developer.android.com/tools/releases/platform-tools |
| MSVC Redistributable | https://aka.ms/vs/17/release/vc_redist.x64.exe |
| Bliss OS ISO | https://blissos.org/ |

---

## Building the Installer

```powershell
# Option 1: GUI
# Open installer\droidbox.iss in Inno Setup Compiler → Compile

# Option 2: Command line
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\droidbox.iss
```

Output: `installer\Output\DroidBoxSetup.exe`

---

## Verifying the Installer

1. Check file size (should be ~3-5 GB)
2. Run on a clean Windows VM
3. Verify all files are installed
4. Launch DroidBox from Start Menu
5. Test .apk file association
6. Run uninstaller
