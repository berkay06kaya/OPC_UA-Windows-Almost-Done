#pragma once

class QGuiApplication;

namespace gui {
void installUnixSignalHandlers(QGuiApplication& app);
}
