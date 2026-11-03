#pragma once

#include <QList>
#include <QString>

/** @file gamepad_probe.h
 * @brief Reports locally attached XInput controller slots without forwarding input to Android.
 */
class GamepadProbe final {
public:
    /** Return connected XInput slots (0–3); this does not enable guest mapping. */
    static QList<int> connectedSlots();
    /** Return a clear explanation of the intentionally limited Phase 10 scope. */
    static QString capabilitySummary();
};
