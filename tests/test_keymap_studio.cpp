#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "gamepad_probe.h"
#include "keymap_editor_model.h"
#include "keymap_profile.h"

namespace {
int failures = 0;
void check(const bool condition, const QString& name)
{
    QTextStream(stdout) << (condition ? "PASS " : "FAIL ") << name << Qt::endl;
    if (!condition) ++failures;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    KeymapProfile initial;
    initial.name = QStringLiteral("Studio fixture");
    initial.resolution = {1080, 1920};
    KeyAction tap; tap.type = KeyAction::Tap; tap.x = 300; tap.y = 400;
    initial.bindings.insert(Qt::Key_F, tap);
    KeymapEditorModel editor(initial);
    QString error;
    check(!editor.setBinding(Qt::Key_F, tap, false, &error) && error.contains("already"), "duplicate key is rejected before overwrite");
    KeyAction swipe; swipe.type = KeyAction::Swipe; swipe.startX = 100; swipe.startY = 200; swipe.endX = 900; swipe.endY = 1600; swipe.durationMs = 300;
    check(editor.setBinding(Qt::Key_G, swipe, false, &error), "new swipe binding is accepted");
    check(editor.profile().bindings.value(Qt::Key_G).type == KeyAction::Swipe && editor.canUndo(), "editor records undo history");
    check(editor.undo() && !editor.profile().bindings.contains(Qt::Key_G), "undo restores prior profile");
    check(editor.redo() && editor.profile().bindings.contains(Qt::Key_G), "redo restores accepted edit");
    check(!editor.changeBindingKey(Qt::Key_G, Qt::Key_F, &error) && error.contains("already"), "key reassignment detects conflict");
    check(editor.changeBindingKey(Qt::Key_G, Qt::Key_H, &error) && editor.profile().bindings.contains(Qt::Key_H), "key reassignment succeeds when unique");
    check(editor.removeBinding(Qt::Key_H) && !editor.profile().bindings.contains(Qt::Key_H), "binding removal is undoable editor state");
    KeymapProfile invalidProfile;
    invalidProfile.resolution = {};
    check(!editor.setProfile(invalidProfile, &error) && error.contains("resolution"), "invalid profile resolution is rejected");
    QTemporaryDir temporary;
    const QString path = temporary.filePath("local-profile.json");
    check(editor.profile().saveToFile(path, &error), "editor profile saves as documented JSON");
    const auto reloaded = KeymapProfile::loadFromFile(path, &error);
    check(reloaded && reloaded->bindings.contains(Qt::Key_F), "saved studio profile reloads through runtime parser");
    const QList<int> controllerIndices = GamepadProbe::connectedSlots();
    bool legalSlots = true; for (const int index : controllerIndices) legalSlots = legalSlots && index >= 0 && index <= 3;
    check(legalSlots && GamepadProbe::capabilitySummary().contains("mapping is not enabled"), "gamepad probe is detection-only and limited to XInput slots");
    return failures == 0 ? 0 : 1;
}
