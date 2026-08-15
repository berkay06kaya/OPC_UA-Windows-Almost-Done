#include "qt/GuiSelfTest.h"

#include <QGuiApplication>
#include <QTimer>
#include <QVariantMap>
#include <QStringList>
#include <iostream>

#include "core/Logger.h"
#include "qt/OpcUaController.h"

int gui::runSelfTest(QGuiApplication& app, OpcUaController& controller) {
    const QStringList args = app.arguments();
    const int st = args.indexOf(QStringLiteral("--selftest"));
    if (st < 0 || args.size() < st + 4) {
        LOG_ERROR() << "kullanim: OPC --selftest <url> <index> <saniye> [kullanici] [sifre]\n"
                       "  index < 0  => sadece endpoint listesini yazar, baglanmaz";
        return 2;
    }
    const QString url  = args[st + 1];
    const int index    = args[st + 2].toInt();
    const int seconds  = args[st + 3].toInt();
    const QString user = (args.size() > st + 4) ? args[st + 4] : QString();
    const QString pass = (args.size() > st + 5) ? args[st + 5] : QString();

    QObject::connect(&controller, &OpcUaController::endpointsChanged, &controller,
                     [&controller, index, user, pass]() {
        const QVariantList eps = controller.endpoints();
        if (eps.isEmpty()) return;
        std::cout << "\n===== BULUNAN ENDPOINT'LER (" << eps.size() << ") =====" << std::endl;
        for (int i = 0; i < eps.size(); ++i) {
            const QVariantMap m = eps[i].toMap();
            std::cout << "  [" << i << "] "
                      << m.value(QStringLiteral("mode")).toString().toStdString() << " / "
                      << m.value(QStringLiteral("policy")).toString().toStdString()
                      << (m.value(QStringLiteral("supported")).toBool()
                              ? "" : "   << ISTEMCI DESTEKLEMIYOR >>")
                      << std::endl;
        }
        std::cout << "=====================================\n" << std::endl;

        if (index >= 0)
            controller.connectToEndpoint(index, user, pass);
        else
            std::cout << "[SELFTEST] index<0: yalnizca listeleme yapildi, baglanti denenmedi." << std::endl;
    });

    QTimer::singleShot(0, &controller, [&controller, url]() { controller.discover(url); });
    QTimer::singleShot(seconds * 1000, &app, &QGuiApplication::quit);

    const int rc = app.exec();
    std::cout << "[SELFTEST] event loop bitti, controller yikiliyor..." << std::endl;
    return rc;
}
