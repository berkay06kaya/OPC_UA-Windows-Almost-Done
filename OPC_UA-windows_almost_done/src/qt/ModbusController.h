#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/GatewayManager.h"
#include "qt/WatchRow.h"

class ModbusConverter;
class ModbusSink;
class WatchedValuesModel;
struct TagValue;
struct ModbusMapping;

class ModbusController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList modbusFormats READ modbusFormats CONSTANT)
    Q_PROPERTY(QVariantList modbusRegisterTypes READ modbusRegisterTypes CONSTANT)
    Q_PROPERTY(int inputRegisterTypeIndex READ inputRegisterTypeIndex CONSTANT)
    Q_PROPERTY(bool modbusServerRunning READ modbusServerRunning NOTIFY modbusServerRunningChanged)

public:
    ModbusController(GatewayManager& manager,
                      std::vector<WatchRow>& watched,
                      std::unordered_map<std::string, std::size_t>& watchedIndex,
                      WatchedValuesModel* valuesModel,
                      QObject* parent = nullptr);

    QVariantList modbusFormats() const;
    QVariantList modbusRegisterTypes() const;
    int inputRegisterTypeIndex() const;
    bool modbusServerRunning() const { return m_serverRunning; }

    Q_INVOKABLE QVariantMap modbusMappingFor(int index) const;
    Q_INVOKABLE void setModbusMapping(int index, int registerAddress, int formatIndex,
                                       int registerTypeIndex, int registerCount);
    Q_INVOKABLE int modbusFixedRegisterCount(int formatIndex) const;
    Q_INVOKABLE QString modbusOversizeWarning(int index, int formatIndex) const;
    Q_INVOKABLE void startModbusServer(const QString& bindAddress, int port, int unitId);
    Q_INVOKABLE void stopModbusServer();
    Q_INVOKABLE void convertAllWatchedTags(int startAddress, int formatIndex,
                                            int registerTypeIndex, int registerCount);

    void refreshPreview(WatchRow& row, const TagValue& value);
    void clearMappingForNode(const std::string& nodeId);

signals:
    void modbusServerRunningChanged();
    void logMessage(const QString& msg);

private:
    void applyMapping(WatchRow& row, const ModbusMapping& mapping);

    GatewayManager& m_manager;
    std::vector<WatchRow>& m_watched;
    std::unordered_map<std::string, std::size_t>& m_watchedIndex;
    WatchedValuesModel* m_valuesModel;

    ModbusConverter* m_converter = nullptr;
    ModbusSink* m_sink = nullptr;
    bool m_serverRunning = false;
};
