#pragma once

class QGuiApplication;
class OpcUaController;

namespace gui {
int run(QGuiApplication& app, OpcUaController& controller);
}
