#include "qt/OpcUaController.h"
#include "source/EndpointResolver.h"
#include "core/Logger.h"

#include <QTime>
#include <QVariantMap>
#include <iostream>

OpcUaController::OpcUaController(GatewayManager& manager, QObject* parent)
    : QObject(parent),
      m_manager(manager),
      m_valuesModel(new WatchedValuesModel(m_watched, this)),
      m_modbusController(new ModbusController(m_manager, m_watched, m_watchedIndex, m_valuesModel, this)) {
    connect(m_modbusController, &ModbusController::logMessage, this, &OpcUaController::appendLog);

    logging::setSink([this](bool , const std::string& line) {
        QMetaObject::invokeMethod(
            this,
            [this, line] {
                m_log.append(QString::fromStdString(line) + QLatin1Char('\n'));
                const int maxChars = 8000;
                if (m_log.size() > maxChars) m_log = m_log.right(maxChars);
                emit logChanged();
            },
            Qt::QueuedConnection);
    });

    m_manager.setPhaseObserver([this](int phase, std::uint32_t status) {
        QMetaObject::invokeMethod(
            this, [this, phase, status] { applyPhase(phase, status); },
            Qt::QueuedConnection);
    });
    m_manager.setTagObserver(
        [this](const std::string& nodeId, const TagValue& value) {
            QMetaObject::invokeMethod(
                this, [this, nodeId, value] { onValueChanged(nodeId, value); },
                Qt::QueuedConnection);
        });
    m_manager.setChunkSizeObserver(
        [this](size_t effectiveSize, bool fromServer, std::uint32_t serverDeclaredValue) {
            QMetaObject::invokeMethod(
                this,
                [this, effectiveSize, fromServer, serverDeclaredValue] {
                    m_serverCapacityText = fromServer
                        ? QStringLiteral("Sunucu kapasitesi: aynı anda %1 tag (sunucu bildirdi: %2, güvenlik tavanı: 200)")
                              .arg(effectiveSize).arg(serverDeclaredValue)
                        : QStringLiteral("Sunucu kapasitesi bildirilmedi, varsayılan kullanılacak: aynı anda %1 tag")
                              .arg(effectiveSize);
                    emit serverCapacityTextChanged();
                    appendLog(m_serverCapacityText);
                },
                Qt::QueuedConnection);
        });
    appendLog(QStringLiteral("Controller hazir."));
}

OpcUaController::~OpcUaController() {
    logging::setSink(nullptr);
    m_manager.stop();
}

QString OpcUaController::connectionStateText() const {
    switch (m_state) {
        case Discovering:  return QStringLiteral("ARANIYOR...");
        case Connecting:   return QStringLiteral("BAGLANIYOR...");
        case Connected:    return QStringLiteral("BAGLI");
        case Reconnecting: return QStringLiteral("YENIDEN BAGLANIYOR...");
        default:           return QStringLiteral("BAGLI DEGIL");
    }
}

void OpcUaController::discover(const QString& url) {
    setState(Discovering);
    appendLog(QStringLiteral("Kapilar araniyor: %1").arg(url));

    EndpointResolver resolver;
    m_rawEndpoints = resolver.getAvailableEndpoints(url.toStdString());

    if (m_rawEndpoints.empty()) {
        m_endpoints.clear();
        emit endpointsChanged();
        appendLog(QStringLiteral("HATA: Sunucuda hicbir ucnokta bulunamadi."));
        setState(Disconnected);
        return;
    }

    QVariantList list;
    for (const EndpointInfo& e : m_rawEndpoints) {
        QVariantMap item;
        item[QStringLiteral("mode")]      = QString::fromStdString(e.securityModeString);
        item[QStringLiteral("policy")]    = QString::fromStdString(e.getShortSecurityPolicy());
        item[QStringLiteral("url")]       = QString::fromStdString(e.endpointUrl);
        item[QStringLiteral("supported")] = e.isSupportedByClient();
        list.push_back(item);
    }
    m_endpoints = list;
    emit endpointsChanged();

    appendLog(QStringLiteral("%1 ucnokta bulundu.").arg(m_rawEndpoints.size()));
    setState(Disconnected);
}

void OpcUaController::connectToEndpoint(int index, const QString& user, const QString& pass) {
    if (index < 0 || index >= static_cast<int>(m_rawEndpoints.size())) {
        appendLog(QStringLiteral("HATA: Gecersiz ucnokta secimi (%1).").arg(index));
        return;
    }
    if (!m_rawEndpoints[index].isSupportedByClient()) {
        appendLog(QStringLiteral("HATA: Bu guvenlik politikasi (%1) istemcide desteklenmiyor.")
                      .arg(QString::fromStdString(m_rawEndpoints[index].getShortSecurityPolicy())));
        return;
    }

    appendLog(QStringLiteral("Baglaniliyor: [%1] %2 / %3")
                  .arg(index)
                  .arg(QString::fromStdString(m_rawEndpoints[index].securityModeString))
                  .arg(QString::fromStdString(m_rawEndpoints[index].getShortSecurityPolicy())));

    m_manager.stop();
    m_manager.source()->setConnectionConfig(m_rawEndpoints[index], user.toStdString(), pass.toStdString());

    setState(Connecting);
    if (!m_manager.start()) {
        appendLog(QStringLiteral("Baglanti baslatilamadi."));
    }
}

void OpcUaController::disconnectFromServer() {
    unsubscribeAll();
    m_manager.stop();
    appendLog(QStringLiteral("Baglanti kullanici istegiyle kapatildi."));
    setState(Disconnected);
    m_rawEndpoints.clear();
    m_endpoints.clear();
    emit endpointsChanged();
}

void OpcUaController::applyPhase(int phase, std::uint32_t ) {
    switch (static_cast<SourcePhase>(phase)) {
        case SourcePhase::Connected:
            setState(Connected);
            browseRoot();
            break;
        case SourcePhase::Reconnecting:
            setState(Reconnecting);
            break;
        case SourcePhase::Connecting:
            setState(Connecting);
            break;
        case SourcePhase::Failed: {
            const QString code = QString::fromStdString(m_manager.source()->lastStatusName());
            const std::string hintStd = m_manager.source()->connectionHint();
            const QString hint = hintStd.empty() ? QString()
                                                  : (QStringLiteral(" -> ") + QString::fromStdString(hintStd));
            appendLog(QStringLiteral("BAGLANTI BASARISIZ (%1)%2").arg(code).arg(hint));
            m_manager.stop();
            setState(Disconnected);
            m_treePath.clear();
            m_treeChildren.clear();
            m_treeChildrenVm.clear();
            m_treeBusy = false;
            emit treeChanged();
            m_valuesModel->reset([&] {
                m_watched.clear();
                m_watchedIndex.clear();
            });
            break;
        }
        case SourcePhase::Idle:
        default:
            break;
    }
}

void OpcUaController::setState(int state) {
    if (m_state == state) return;
    m_state = state;
    std::cout << "[STATE] " << connectionStateText().toStdString() << std::endl;
    emit connectionStateChanged();
}

void OpcUaController::appendLog(const QString& msg) {
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                             .arg(msg);
    std::cout << line.toStdString() << std::endl;

    m_log.append(line + QLatin1Char('\n'));
    const int maxChars = 8000;
    if (m_log.size() > maxChars)
        m_log = m_log.right(maxChars);
    emit logChanged();
}

QString OpcUaController::treePath() const {
    QString s;
    for (size_t i = 0; i < m_treePath.size(); ++i) {
        if (i) s += QStringLiteral(" > ");
        s += QString::fromStdString(m_treePath[i].second);
    }
    return s;
}

void OpcUaController::browseRoot() {
    m_treePath.clear();
    m_treePath.push_back({std::string(), std::string("Objects")});
    requestChildren(m_treePath.back().first);
}

void OpcUaController::browseInto(int index) {
    if (index < 0 || index >= static_cast<int>(m_treeChildren.size())) return;
    const BrowseNode& sel = m_treeChildren[index];
    if (!sel.expandable) {
        if (m_watchedIndex.count(sel.nodeId)) {
            appendLog(QStringLiteral("Zaten izleniyor: %1")
                          .arg(QString::fromStdString(sel.displayName)));
            return;
        }
        std::string logicalName;
        for (size_t i = 1; i < m_treePath.size(); ++i)
            logicalName += m_treePath[i].second + " / ";
        logicalName += sel.displayName;

        const int newIndex = static_cast<int>(m_watched.size());
        m_valuesModel->insertRows(newIndex, newIndex, [&] {
            WatchRow row;
            row.nodeId = sel.nodeId;
            row.logicalName = logicalName;
            m_watchedIndex[row.nodeId] = m_watched.size();
            m_watched.push_back(std::move(row));
        });
        m_manager.source()->subscribe(sel.nodeId, logicalName);
        appendLog(QStringLiteral("Abone olundu: %1  nodeId=%2")
                      .arg(QString::fromStdString(logicalName))
                      .arg(QString::fromStdString(sel.nodeId)));
        return;
    }
    m_treePath.push_back({sel.nodeId, sel.displayName});
    requestChildren(sel.nodeId);
}

void OpcUaController::subscribeAllInCurrentFolder() {
    struct Pending {
        std::string nodeId;
        std::string logicalName;
    };
    std::vector<Pending> pending;

    for (const BrowseNode& sel : m_treeChildren) {
        if (sel.expandable) continue;
        if (m_watchedIndex.count(sel.nodeId)) continue;

        std::string logicalName;
        for (size_t i = 1; i < m_treePath.size(); ++i)
            logicalName += m_treePath[i].second + " / ";
        logicalName += sel.displayName;
        pending.push_back({sel.nodeId, logicalName});
    }

    if (!pending.empty()) {
        const int first = static_cast<int>(m_watched.size());
        const int last = first + static_cast<int>(pending.size()) - 1;
        m_valuesModel->insertRows(first, last, [&] {
            for (const Pending& p : pending) {
                WatchRow row;
                row.nodeId = p.nodeId;
                row.logicalName = p.logicalName;
                m_watchedIndex[row.nodeId] = m_watched.size();
                m_watched.push_back(std::move(row));
            }
        });
        std::vector<std::pair<std::string, std::string>> toSubscribe;
        toSubscribe.reserve(pending.size());
        for (const Pending& p : pending)
            toSubscribe.push_back({p.nodeId, p.logicalName});
        m_manager.source()->subscribeMany(toSubscribe);
    }

    appendLog(QStringLiteral("Toplu abone olma: %1 yeni tag eklendi.").arg(static_cast<int>(pending.size())));
}

void OpcUaController::browseUp() {
    if (m_treePath.size() <= 1) return;
    m_treePath.pop_back();
    requestChildren(m_treePath.back().first);
}

void OpcUaController::requestChildren(const std::string& nodeId) {
    if (!m_manager.source()->isConnected()) return;
    m_treeBusy = true;
    emit treeChanged();
    m_manager.source()->browseChildrenAsync(nodeId, [this](std::vector<BrowseNode> res) {
        QMetaObject::invokeMethod(
            this, [this, res]() { onChildrenReady(res); }, Qt::QueuedConnection);
    });
}

void OpcUaController::onChildrenReady(const std::vector<BrowseNode>& children) {
    m_treeChildren = children;
    m_treeChildrenVm.clear();
    for (const BrowseNode& c : children) {
        const char* cls = c.nodeClass == BrowseNode::Class::Object   ? "Object"
                        : c.nodeClass == BrowseNode::Class::Variable  ? "Variable"
                        : c.nodeClass == BrowseNode::Class::Method    ? "Method" : "Other";
        QVariantMap m;
        m[QStringLiteral("displayName")] = QString::fromStdString(c.displayName);
        m[QStringLiteral("browseName")]  = QString::fromStdString(c.browseName);
        m[QStringLiteral("nodeId")]      = QString::fromStdString(c.nodeId);
        m[QStringLiteral("nodeClass")]   = QString::fromUtf8(cls);
        m[QStringLiteral("expandable")]  = c.expandable;
        m_treeChildrenVm.push_back(m);
    }
    m_treeBusy = false;
    appendLog(QStringLiteral("Agac: %1 dugum (%2)").arg(children.size()).arg(treePath()));
    emit treeChanged();
}

void OpcUaController::onValueChanged(const std::string& nodeId, const TagValue& value) {
    auto it = m_watchedIndex.find(nodeId);
    if (it == m_watchedIndex.end()) return;

    WatchRow& w = m_watched[it->second];
    w.value = value.toString();
    w.good = (value.quality == Quality::Good);
    w.hasValue = true;
    m_modbusController->refreshPreview(w, value);
    m_valuesModel->notifyRowChanged(static_cast<int>(it->second));
}

void OpcUaController::unsubscribeNode(int index) {
    if (index < 0 || index >= static_cast<int>(m_watched.size())) return;
    const std::string nodeId = m_watched[index].nodeId;
    const QString name = QString::fromStdString(m_watched[index].logicalName);
    m_manager.source()->unsubscribe(nodeId);

    m_modbusController->clearMappingForNode(nodeId);

    m_valuesModel->removeRow(index, [&] {
        m_watched.erase(m_watched.begin() + index);
        rebuildWatchedIndex();
    });
    appendLog(QStringLiteral("Izleme birakildi: %1").arg(name));
}

void OpcUaController::unsubscribeAll() {
    if (m_watched.empty()) return;

    std::vector<std::string> nodeIds;
    nodeIds.reserve(m_watched.size());
    for (const WatchRow& w : m_watched) {
        nodeIds.push_back(w.nodeId);
        m_modbusController->clearMappingForNode(w.nodeId);
    }

    m_manager.source()->unsubscribeMany(nodeIds);

    const int count = static_cast<int>(m_watched.size());
    m_valuesModel->reset([&] {
        m_watched.clear();
        m_watchedIndex.clear();
    });
    appendLog(QStringLiteral("Toplu cikarma: %1 tag izlemeden cikarildi.").arg(count));
}

void OpcUaController::renameNode(int index, const QString& newName) {
    if (index < 0 || index >= static_cast<int>(m_watched.size())) return;
    const std::string trimmed = newName.trimmed().toStdString();
    if (trimmed.empty()) return;
    if (trimmed == m_watched[index].logicalName) return;
    const QString oldName = QString::fromStdString(m_watched[index].logicalName);
    m_watched[index].logicalName = trimmed;

    m_manager.store().renameTag(m_watched[index].nodeId, trimmed);
    m_valuesModel->notifyRowChanged(index);
    appendLog(QStringLiteral("Ad degistirildi: %1 -> %2")
                  .arg(oldName)
                  .arg(QString::fromStdString(trimmed)));
}

void OpcUaController::rebuildWatchedIndex() {
    m_watchedIndex.clear();
    for (std::size_t i = 0; i < m_watched.size(); ++i)
        m_watchedIndex[m_watched[i].nodeId] = i;
}

QStringList OpcUaController::converterNames() const {
    QStringList list;
    for (const std::string& name : m_manager.converterNames())
        list << QString::fromStdString(name);
    return list;
}
