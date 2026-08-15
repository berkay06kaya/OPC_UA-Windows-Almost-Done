#include "qt/GuiApp.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QUrl>

#include "core/Logger.h"
#include "qt/OpcUaController.h"
#include "qt/GuiSelfTest.h"
#include "qt/UnixSignalHandler.h"

int gui::run(QGuiApplication& app, OpcUaController& controller) {
    gui::installUnixSignalHandlers(app);

    if (app.arguments().contains(QStringLiteral("--selftest")))
        return gui::runSelfTest(app, controller);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("opc"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/src/qt/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        LOG_ERROR() << "[-] QML yuklenemedi!";
        return 1;
    }
    return app.exec();
}
