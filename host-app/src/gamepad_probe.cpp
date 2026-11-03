#include "gamepad_probe.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Xinput.h>
#endif

QList<int> GamepadProbe::connectedSlots()
{
    QList<int> controllerIndices;
#ifdef Q_OS_WIN
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    HMODULE module = LoadLibraryW(L"xinput1_4.dll");
    if (!module) module = LoadLibraryW(L"xinput9_1_0.dll");
    if (!module) return controllerIndices;
    const auto getState = reinterpret_cast<GetState>(GetProcAddress(module, "XInputGetState"));
    if (getState) for (DWORD index = 0; index < 4; ++index) { XINPUT_STATE state{}; if (getState(index, &state) == ERROR_SUCCESS) controllerIndices.append(static_cast<int>(index)); }
    FreeLibrary(module);
#endif
    return controllerIndices;
}

QString GamepadProbe::capabilitySummary()
{
    const QList<int> controllerIndices = connectedSlots();
    return controllerIndices.isEmpty()
        ? QStringLiteral("No XInput controller detected. Gamepad-to-Android mapping is not enabled in this release.")
        : QStringLiteral("Detected XInput controller slot(s): %1. Detection only; gamepad-to-Android mapping is not enabled yet.")
              .arg([&controllerIndices] { QStringList values; for (const int index : controllerIndices) values << QString::number(index + 1); return values.join(", "); }());
}
