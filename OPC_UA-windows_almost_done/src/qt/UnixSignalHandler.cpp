#include "qt/UnixSignalHandler.h"

#include <QGuiApplication>

#include <csignal>

#ifdef _WIN32

#include <windows.h>

namespace {
QGuiApplication* g_app = nullptr;

BOOL WINAPI winCtrlHandler(DWORD) {
    if (g_app) QMetaObject::invokeMethod(g_app, "quit", Qt::QueuedConnection);
    return TRUE;
}
}

namespace gui {

void installUnixSignalHandlers(QGuiApplication& app) {
    g_app = &app;
    SetConsoleCtrlHandler(winCtrlHandler, TRUE);
}

}

#else

#include <QSocketNotifier>

#include <sys/socket.h>
#include <unistd.h>

namespace {
int g_sigFd[2] = {-1, -1};

void unixSignalHandler(int) {
    char a = 1;
    ::write(g_sigFd[0], &a, sizeof(a));
}
}

namespace gui {

void installUnixSignalHandlers(QGuiApplication& app) {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd) != 0)
        return;

    std::signal(SIGINT, unixSignalHandler);
    std::signal(SIGTERM, unixSignalHandler);

    auto* notifier = new QSocketNotifier(g_sigFd[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [&app]() {
        char a;
        ::read(g_sigFd[1], &a, sizeof(a));
        app.quit();
    });
}

}

#endif
