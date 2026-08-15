#pragma once

class QGuiApplication;
class OpcUaController;

namespace gui {
int runSelfTest(QGuiApplication& app, OpcUaController& controller);
}
