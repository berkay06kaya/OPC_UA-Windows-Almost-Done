#include <QGuiApplication>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/GatewayManager.h"
#include "source/OpcUaDataSource.h"
#include "sink/ModbusConverter.h"
#include "sink/ModbusSink.h"
#include "qt/OpcUaController.h"
#include "qt/GuiApp.h"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral(" OPC UA"));

    GatewayManager manager;
    manager.setDataSource(std::make_unique<OpcUaDataSource>(&manager.store()));
    auto modbusConverter = std::make_unique<ModbusConverter>();
    ModbusConverter* modbusConverterPtr = modbusConverter.get();
    manager.setConverter<std::vector<std::uint16_t>>("Modbus", std::move(modbusConverter));
    manager.addSink("Modbus", std::make_unique<ModbusSink>(&manager.store(), modbusConverterPtr));

    OpcUaController controller(manager);

    return gui::run(app, controller);
}
