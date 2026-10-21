#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "display_embed.h"
#include "input_handler.h"
#include "qemu_controller.h"

#include <functional>

#define NOMINMAX
#include <Windows.h>

namespace {
int failures = 0;
void check(bool success, const QString& name)
{
    QTextStream(stdout) << (success ? "PASS " : "FAIL ") << name << Qt::endl;
    if (!success) ++failures;
}
bool waitUntil(const std::function<bool()>& condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 7000) {
        QCoreApplication::processEvents(); QThread::msleep(5);
    }
    return condition();
}
QList<QJsonObject> events(const QString& path)
{
    QList<QJsonObject> result;
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return result;
    while (!file.atEnd()) {
        const auto command = QJsonDocument::fromJson(file.readLine()).object();
        for (const auto& event : command.value("arguments").toObject().value("events").toArray()) result.append(event.toObject());
    }
    return result;
}
bool keyMatches(const QJsonObject& event, int code, bool down)
{
    const auto data = event.value("data").toObject();
    return event.value("type") == "key" && data.value("key").toObject().value("data").toInt() == code && data.value("down").toBool() == down;
}
int mockQemu(QApplication& application, const QStringList& arguments)
{
    const auto endpoint = arguments.value(arguments.indexOf("-qmp") + 1);
    const int port = endpoint.section(':', 2, 2).section(',', 0, 0).toInt();
    // A plain foreign Win32 window models SDL without Qt's own focus policy
    // interfering when the fixture is reparented into the parent test process.
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"DroidBoxNativeKeyboardFixture";
    RegisterClassW(&windowClass);
    HWND window = CreateWindowW(windowClass.lpszClassName, L"Native input protocol fixture", WS_OVERLAPPEDWINDOW,
                                50, 50, 320, 240, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window) return 2;
    ShowWindow(window, SW_SHOW);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) return 2;
    QObject::connect(&server, &QTcpServer::newConnection, &application, [&] {
        auto* socket = server.nextPendingConnection();
        socket->write("{\"QMP\":{\"version\":{\"qemu\":{\"major\":9,\"minor\":0,\"micro\":0},\"package\":\"test\"},\"capabilities\":[]}}\r\n");
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, port] {
            while (socket->canReadLine()) {
                const auto bytes = socket->readLine();
                const auto command = QJsonDocument::fromJson(bytes).object();
                if (command.value("execute") == "input-send-event") {
                    QFile log(QDir(qEnvironmentVariable("DROIDBOX_NATIVE_TEST_LOG")).filePath(QString::number(port) + ".jsonl"));
                    if (log.open(QIODevice::Append)) log.write(bytes);
                }
                socket->write(QJsonDocument(QJsonObject{{"return", QJsonObject{}}, {"id", command.value("id")}}).toJson(QJsonDocument::Compact) + '\n');
            }
        });
    });
    return application.exec();
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.contains("-qmp")) return mockQemu(application, arguments);
    QApplication::setQuitOnLastWindowClosed(false);
    QTemporaryDir temporary; if (!temporary.isValid()) return 1;
    qputenv("DROIDBOX_NATIVE_TEST_LOG", temporary.path().toUtf8());
    const auto allocatePort = [] { QTcpServer reservation; if (!reservation.listen(QHostAddress::LocalHost, 0)) return quint16{0}; return reservation.serverPort(); };
    const int firstPort = allocatePort();
    int secondPort = allocatePort(); while (secondPort == firstPort) secondPort = allocatePort();
    const auto prepare = [&](const QString& name, int port) {
        const QString disk = temporary.filePath(name + ".qcow2"), config = temporary.filePath(name + ".json");
        QFile image(disk);
        if (!image.open(QIODevice::WriteOnly) || image.write("protocol fixture, not a guest image") < 0) return QString{};
        image.close();
        QFile file(config); if (!file.open(QIODevice::WriteOnly)) return QString{};
        file.write(QJsonDocument(QJsonObject{{"cpuCores", 1}, {"ramMB", 1024}, {"qmpPort", port}, {"adbHostPort", port == 5555 ? 5556 : 5555},
            {"qemuPath", QCoreApplication::applicationFilePath()}, {"overlayImage", disk}}).toJson());
        return config;
    };
    QemuController first, second;
    DisplayEmbed displayA, displayB;
    displayA.setWindowTitle("Native keyboard test A"); displayB.setWindowTitle("Native keyboard test B");
    displayA.resize(400, 320); displayB.resize(400, 320); displayB.move(450, 50);
    InputHandler inputA, inputB;
    inputA.installOn(&displayA, &first); inputB.installOn(&displayB, &second);
    displayA.show(); displayB.show();
    check(first.start(prepare("first", firstPort)) && second.start(prepare("second", secondPort)), "start isolated QMP protocol processes");
    if (!waitUntil([&] { return first.getStatus() == "running" && second.getStatus() == "running"; })) return 1;
    quintptr windowA = 0, windowB = 0;
    check(waitUntil([&] { windowA = DisplayEmbed::findWindowForProcess(first.processId()); windowB = DisplayEmbed::findWindowForProcess(second.processId()); return windowA && windowB; }), "find both native process windows");
    if (!windowA || !windowB) return 1;
    check(displayA.embedWindow(windowA) && displayB.embedWindow(windowB), "embed separate process windows");
    const QString logA = temporary.filePath(QString::number(firstPort) + ".jsonl"), logB = temporary.filePath(QString::number(secondPort) + ".jsonl");
    const auto focus = [](DisplayEmbed& display) {
        display.showNormal(); display.raise(); display.activateWindow(); display.setFocus();
        QCoreApplication::processEvents();
        const HWND child = reinterpret_cast<HWND>(display.embeddedWindow());
        const HWND root = GetAncestor(child, GA_ROOT);
        const DWORD current = GetCurrentThreadId();
        const DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const bool attached = current != foreground && AttachThreadInput(current, foreground, TRUE);
        SetForegroundWindow(root); display.focusEmbeddedWindow();
        if (attached) AttachThreadInput(current, foreground, FALSE);
    };
    focus(displayA);
    check(!inputA.isKeymapEnabled() && !inputB.isKeymapEnabled(), "ordinary typing is tested with keymaps disabled");
    check(InputHandler::dispatchNativeKeyEvent('A', true) && InputHandler::dispatchNativeKeyEvent('A', false), "ordinary native press and release route through QMP");
    check(waitUntil([&] { return events(logA).size() == 2; }) && events(logB).isEmpty(), "only first focused guest receives A");
    auto firstEvents = events(logA);
    check(firstEvents.size() == 2 && keyMatches(firstEvents[0], 30, true) && keyMatches(firstEvents[1], 30, false), "forward exact XT key code and press/release states");
    focus(displayB);
    check(InputHandler::dispatchNativeKeyEvent('B', true) && InputHandler::dispatchNativeKeyEvent('B', false), "switch native input to second guest");
    check(waitUntil([&] { return events(logB).size() == 2; }) && events(logA).size() == 2, "inactive guest receives no B events");
    focus(displayA);
    check(InputHandler::dispatchNativeKeyEvent(0x10, true), "hold Shift in first guest");
    focus(displayB);
    check(InputHandler::dispatchNativeKeyEvent(0x10, true), "held-key repeat can be owned by another guest after switching");
    check(InputHandler::dispatchNativeKeyEvent(0x10, false), "one key-up releases all guest owners");
    check(waitUntil([&] { return events(logA).size() == 4 && events(logB).size() == 4; })
        && keyMatches(events(logA).last(), 42, false) && keyMatches(events(logB).last(), 42, false), "neither guest retains a stuck modifier");
    check(!InputHandler::dispatchNativeKeyEvent(0xff, true), "unsupported keys are not consumed");
    QLineEdit hostEditor;
    hostEditor.setWindowTitle("Native keyboard host editor"); hostEditor.show(); hostEditor.activateWindow(); hostEditor.setFocus();
    SetForegroundWindow(reinterpret_cast<HWND>(hostEditor.winId())); QCoreApplication::processEvents();
    check(!InputHandler::dispatchNativeKeyEvent('C', true) && !InputHandler::dispatchNativeKeyEvent('C', false), "host dialogs and edits keep their normal keyboard input");
    first.kill(); second.kill(); first.waitForExit(); second.waitForExit();
    displayA.clearEmbeddedWindow(); displayB.clearEmbeddedWindow();
    QTextStream(stdout) << "Failures: " << failures << Qt::endl;
    return failures ? 1 : 0;
}
