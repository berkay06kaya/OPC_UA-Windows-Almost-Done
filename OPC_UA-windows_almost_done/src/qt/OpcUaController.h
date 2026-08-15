#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>

#include "core/GatewayManager.h"
#include "core/IOpcUaSource.h"
#include "core/EndpointInfo.h"
#include "core/BrowseNode.h"
#include "core/TagValue.h"
#include "qt/WatchRow.h"
#include "qt/WatchedValuesModel.h"
#include "qt/ModbusController.h"

class OpcUaController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString connectionStateText READ connectionStateText NOTIFY connectionStateChanged)
    Q_PROPERTY(QVariantList endpoints READ endpoints NOTIFY endpointsChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(QVariantList treeChildren READ treeChildren NOTIFY treeChanged)
    Q_PROPERTY(QString treePath READ treePath NOTIFY treeChanged)
    Q_PROPERTY(int treeDepth READ treeDepth NOTIFY treeChanged)
    Q_PROPERTY(bool treeBusy READ treeBusy NOTIFY treeChanged)
    Q_PROPERTY(QObject* watchedValuesModel READ watchedValuesModel CONSTANT)
    Q_PROPERTY(QStringList converterNames READ converterNames CONSTANT)
    Q_PROPERTY(QObject* modbus READ modbusController CONSTANT)
    Q_PROPERTY(QString serverCapacityText READ serverCapacityText NOTIFY serverCapacityTextChanged)

public:
    enum ConnectionState {
        Disconnected = 0,
        Discovering,
        Connecting,
        Connected,
        Reconnecting
    };
    Q_ENUM(ConnectionState)

    explicit OpcUaController(GatewayManager& manager, QObject* parent = nullptr);
    ~OpcUaController() override;

    int connectionState() const { return m_state; }
    QString connectionStateText() const;
    QVariantList endpoints() const { return m_endpoints; }
    QString log() const { return m_log; }
    QVariantList treeChildren() const { return m_treeChildrenVm; }
    QString treePath() const;
    int treeDepth() const { return static_cast<int>(m_treePath.size()); }
    bool treeBusy() const { return m_treeBusy; }
    QObject* watchedValuesModel() const { return m_valuesModel; }
    QStringList converterNames() const;
    QObject* modbusController() const { return m_modbusController; }
    QString serverCapacityText() const { return m_serverCapacityText; }

    Q_INVOKABLE void discover(const QString& url);
    Q_INVOKABLE void connectToEndpoint(int index, const QString& user, const QString& pass);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void browseRoot();
    Q_INVOKABLE void browseInto(int index);
    Q_INVOKABLE void browseUp();
    Q_INVOKABLE void subscribeAllInCurrentFolder();
    Q_INVOKABLE void unsubscribeNode(int index);
    Q_INVOKABLE void unsubscribeAll();
    Q_INVOKABLE void renameNode(int index, const QString& newName);

signals:
    void connectionStateChanged();
    void endpointsChanged();
    void logChanged();
    void treeChanged();
    void serverCapacityTextChanged();

private:
    void applyPhase(int phase, std::uint32_t status);
    void setState(int state);
    void appendLog(const QString& msg);
    void requestChildren(const std::string& nodeId);
    void onChildrenReady(const std::vector<BrowseNode>& children);

    void onValueChanged(const std::string& nodeId, const TagValue& value);

    void rebuildWatchedIndex();

    GatewayManager& m_manager;
    std::vector<EndpointInfo> m_rawEndpoints;

    int m_state = Disconnected;
    QVariantList m_endpoints;
    QString m_log;

    std::vector<std::pair<std::string, std::string>> m_treePath;
    std::vector<BrowseNode> m_treeChildren;
    QVariantList m_treeChildrenVm;
    bool m_treeBusy = false;

    std::vector<WatchRow> m_watched;
    std::unordered_map<std::string, std::size_t> m_watchedIndex;
    WatchedValuesModel* m_valuesModel = nullptr;

    ModbusController* m_modbusController = nullptr;
    QString m_serverCapacityText;
};
