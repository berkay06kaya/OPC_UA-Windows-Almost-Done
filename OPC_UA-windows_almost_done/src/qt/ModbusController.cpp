#include "qt/ModbusController.h"
#include "qt/WatchedValuesModel.h"
#include "sink/ModbusFormatNames.h"
#include "sink/ModbusSink.h"
#include "sink/ModbusConverter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace {

double magnitudeOf(const TagValue& v) {
    return std::visit([](auto&& x) -> double {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::string>) return 0.0;
        else if constexpr (std::is_same_v<T, bool>) return x ? 1.0 : 0.0;
        else return std::fabs(static_cast<double>(x));
    }, v.value);
}

int neededRegisterTier(double magnitude) {
    if (magnitude <= 65535.0) return 1;
    if (magnitude <= 4294967295.0) return 2;
    return 4;
}

bool coilBitForPreview(const TagValue& v) {
    return std::visit([](auto&& x) -> bool {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate>) return false;
        else if constexpr (std::is_same_v<T, bool>) return x;
        else if constexpr (std::is_same_v<T, std::string>)
            return !x.empty() && x != "0" && x != "false" && x != "False";
        else return x != 0;
    }, v.value);
}

QString formatModbusPreview(const std::vector<std::uint16_t>& regs, const ModbusMapping& mapping) {
    QString s = QStringLiteral("[");
    for (std::size_t i = 0; i < regs.size(); ++i) {
        if (i) s += QLatin1Char(',');
        s += QStringLiteral("%1").arg(regs[i], 4, 16, QLatin1Char('0')).toUpper();
    }
    s += QStringLiteral("]");

    const char* rt = mapping.registerType == ModbusMapping::RegisterType::HoldingRegister ? "HR"
                    : mapping.registerType == ModbusMapping::RegisterType::InputRegister  ? "IR"
                    : mapping.registerType == ModbusMapping::RegisterType::Coil           ? "CO"
                                                                                           : "DI";
    s += QStringLiteral(" @ %1#%2").arg(QString::fromUtf8(rt)).arg(mapping.registerAddress);
    return s;
}

}

ModbusController::ModbusController(GatewayManager& manager,
                                    std::vector<WatchRow>& watched,
                                    std::unordered_map<std::string, std::size_t>& watchedIndex,
                                    WatchedValuesModel* valuesModel,
                                    QObject* parent)
    : QObject(parent),
      m_manager(manager),
      m_watched(watched),
      m_watchedIndex(watchedIndex),
      m_valuesModel(valuesModel),
      m_converter(dynamic_cast<ModbusConverter*>(
          manager.converter<std::vector<std::uint16_t>>("Modbus"))),
      m_sink(manager.sink<ModbusSink>("Modbus")) {
    if (m_sink) {
        m_sink->setStatusCallback([this](bool running, const std::string& msg) {
            QMetaObject::invokeMethod(
                this,
                [this, running, msg] {
                    m_serverRunning = running;
                    emit modbusServerRunningChanged();
                    emit logMessage(QString::fromStdString(msg));
                },
                Qt::QueuedConnection);
        });
    }
}

QVariantList ModbusController::modbusFormats() const {
    QVariantList list;
    int index = 0;
    for (const auto& entry : modbusFormatNames()) {
        QVariantMap m;
        m[QStringLiteral("index")] = index++;
        m[QStringLiteral("name")]  = QString::fromStdString(entry.second);
        list.push_back(m);
    }
    return list;
}

QVariantList ModbusController::modbusRegisterTypes() const {
    QVariantList list;
    int index = 0;
    for (const auto& entry : modbusRegisterTypeNames()) {
        QVariantMap m;
        m[QStringLiteral("index")] = index++;
        m[QStringLiteral("name")]  = QString::fromStdString(entry.second);
        list.push_back(m);
    }
    return list;
}

int ModbusController::inputRegisterTypeIndex() const {
    const auto types = modbusRegisterTypeNames();
    for (std::size_t i = 0; i < types.size(); ++i)
        if (types[i].first == ModbusMapping::RegisterType::InputRegister) return static_cast<int>(i);
    return 0;
}

int ModbusController::modbusFixedRegisterCount(int formatIndex) const {
    const auto formats = modbusFormatNames();
    if (formatIndex < 0 || formatIndex >= static_cast<int>(formats.size())) return 0;
    return ::modbusFixedRegisterCount(formats[static_cast<std::size_t>(formatIndex)].first);
}

QVariantMap ModbusController::modbusMappingFor(int index) const {
    QVariantMap result;
    result[QStringLiteral("hasMapping")] = false;
    if (index < 0 || index >= static_cast<int>(m_watched.size()) || !m_converter)
        return result;

    const std::string& nodeId = m_watched[static_cast<std::size_t>(index)].nodeId;
    if (!m_converter->hasMapping(nodeId)) return result;

    const ModbusMapping mapping = m_converter->mappingFor(nodeId);
    const auto formats = modbusFormatNames();
    const auto types = modbusRegisterTypeNames();

    int formatIndex = 0;
    for (std::size_t i = 0; i < formats.size(); ++i)
        if (formats[i].first == mapping.format) { formatIndex = static_cast<int>(i); break; }

    int typeIndex = 0;
    for (std::size_t i = 0; i < types.size(); ++i)
        if (types[i].first == mapping.registerType) { typeIndex = static_cast<int>(i); break; }

    result[QStringLiteral("hasMapping")]        = true;
    result[QStringLiteral("registerAddress")]   = mapping.registerAddress;
    result[QStringLiteral("registerCount")]     = mapping.registerCount;
    result[QStringLiteral("formatIndex")]       = formatIndex;
    result[QStringLiteral("registerTypeIndex")] = typeIndex;
    return result;
}

QString ModbusController::modbusOversizeWarning(int index, int formatIndex) const {
    if (index < 0 || index >= static_cast<int>(m_watched.size())) return {};
    const auto formats = modbusFormatNames();
    if (formatIndex < 0 || formatIndex >= static_cast<int>(formats.size())) return {};
    const ModbusMapping::Format format = formats[static_cast<std::size_t>(formatIndex)].first;
    if (!::modbusFormatIsPlainInteger(format)) return {};

    const int natural = ::modbusFixedRegisterCount(format);
    const WatchRow& row = m_watched[static_cast<std::size_t>(index)];
    if (!row.hasValue) return {};
    Tag tag;
    if (!m_manager.store().getTag(row.nodeId, tag)) return {};

    const int needed = neededRegisterTier(magnitudeOf(tag.value));
    if (needed < natural) {
        return QStringLiteral("Bu deger su an yalnizca %1 register'a sigiyor, secilen format %2 register kullaniyor. "
                               "Daha ekonomik bir format secebilirsiniz.").arg(needed).arg(natural);
    }
    return {};
}

void ModbusController::startModbusServer(const QString& bindAddress, int port, int unitId) {
    if (!m_sink) return;

    bool anyReady = false;
    for (const WatchRow& w : m_watched) {
        if (w.hasModbusMapping && w.hasValue) { anyReady = true; break; }
    }
    if (!anyReady) {
        const bool noMappingAtAll = !m_converter || m_converter->allMappings().empty();
        emit logMessage(noMappingAtAll
            ? QStringLiteral("HATA: Hicbir tag Modbus'a eslenmemis, sunucu baslatilamadi.")
            : QStringLiteral("HATA: Eslenmis tag(lar) henuz OPC'den deger almadi, sunucu baslatilamadi."));
        return;
    }

    const QString trimmedBind = bindAddress.trimmed();
    if (!trimmedBind.isEmpty()) {
        struct in_addr addr ;
        if (inet_pton(AF_INET, trimmedBind.toStdString().c_str(), &addr) != 1)
        {
            emit logMessage(QStringLiteral("HATA: Dinleme IP adresi geçersiz: '%1'").arg(trimmedBind));
            return;
        }
    }
    m_sink->setBindAddress(bindAddress.toStdString());
    m_sink->setPort(port);
    m_sink->setUnitId(unitId);
    m_manager.startSink("Modbus");
}

void ModbusController::stopModbusServer() {
    m_manager.stopSink("Modbus");
}

void ModbusController::setModbusMapping(int index, int registerAddress, int formatIndex,
                                         int registerTypeIndex, int registerCount) {
    if (index < 0 || index >= static_cast<int>(m_watched.size())) return;
    if (!m_converter) {
        emit logMessage(QStringLiteral("HATA: Modbus donusturucu kurulmamis."));
        return;
    }

    const auto formats = modbusFormatNames();
    const auto types = modbusRegisterTypeNames();
    if (formatIndex < 0 || formatIndex >= static_cast<int>(formats.size())) return;
    if (registerTypeIndex < 0 || registerTypeIndex >= static_cast<int>(types.size())) return;

    WatchRow& row = m_watched[static_cast<std::size_t>(index)];

    ModbusMapping mapping;
    mapping.registerAddress = registerAddress;
    mapping.format          = formats[static_cast<std::size_t>(formatIndex)].first;
    mapping.registerType    = types[static_cast<std::size_t>(registerTypeIndex)].first;

    const int natural = ::modbusFixedRegisterCount(mapping.format);
    int effectiveCount = natural > 0 ? natural : std::max(1, registerCount);

    if (::modbusFormatIsPlainInteger(mapping.format) && row.hasValue) {
        Tag tag;
        if (m_manager.store().getTag(row.nodeId, tag)) {
            const int needed = neededRegisterTier(magnitudeOf(tag.value));
            if (needed > natural) {
                effectiveCount = needed;
                emit logMessage(QStringLiteral("BILGI: Deger secilen formata sigmiyor, otomatik olarak %1 register kullanilacak.")
                              .arg(needed));
            }
        }
    }
    mapping.registerCount = effectiveCount;

    const std::string collidingId = m_converter->collidingNodeId(row.nodeId, mapping);
    if (!collidingId.empty()) {
        QString otherName = QString::fromStdString(collidingId);
        auto otherIt = m_watchedIndex.find(collidingId);
        if (otherIt != m_watchedIndex.end())
            otherName = QString::fromStdString(m_watched[otherIt->second].logicalName);
        emit logMessage(QStringLiteral("HATA: Bu register araligi zaten '%1' etiketine atanmis, kaydedilmedi.")
                      .arg(otherName));
        return;
    }

    applyMapping(row, mapping);
    m_valuesModel->notifyRowChanged(index);
    emit logMessage(QStringLiteral("Donusturucu eslemesi kaydedildi: %1 -> %2 / %3#%4")
                  .arg(QString::fromStdString(row.logicalName))
                  .arg(QString::fromStdString(formats[static_cast<std::size_t>(formatIndex)].second))
                  .arg(QString::fromStdString(types[static_cast<std::size_t>(registerTypeIndex)].second))
                  .arg(registerAddress));
}

void ModbusController::applyMapping(WatchRow& row, const ModbusMapping& mapping) {
    if (m_converter->hasMapping(row.nodeId) && m_sink) {
        const ModbusMapping oldMapping = m_converter->mappingFor(row.nodeId);
        m_sink->onMappingRemoved(oldMapping);
    }

    m_converter->setMapping(row.nodeId, mapping);
    row.hasModbusMapping = true;

    Tag tag;
    if (m_manager.store().getTag(row.nodeId, tag)) {
        refreshPreview(row, tag.value);
        if (m_sink) m_sink->onTagUpdate(row.nodeId, tag.value);
    }
}

void ModbusController::convertAllWatchedTags(int startAddress, int formatIndex,
                                              int registerTypeIndex, int registerCount) {
    if (!m_converter) {
        emit logMessage(QStringLiteral("HATA: Modbus donusturucu kurulmamis."));
        return;
    }

    const auto formats = modbusFormatNames();
    const auto types = modbusRegisterTypeNames();
    if (formatIndex < 0 || formatIndex >= static_cast<int>(formats.size())) return;
    if (registerTypeIndex < 0 || registerTypeIndex >= static_cast<int>(types.size())) return;

    if (m_watched.empty()) {
        emit logMessage(QStringLiteral("BILGI: Hicbir tag izlenmiyor, toplu donusum yapilamadi."));
        return;
    }

    const ModbusMapping::Format format = formats[static_cast<std::size_t>(formatIndex)].first;
    const ModbusMapping::RegisterType registerType = types[static_cast<std::size_t>(registerTypeIndex)].first;
    const int natural = ::modbusFixedRegisterCount(format);
    const bool isPlainInt = ::modbusFormatIsPlainInteger(format);

    int cursor = startAddress;
    int convertedCount = 0;
    int oversizeBumpCount = 0;
    int collisionSkipCount = 0;

    for (std::size_t i = 0; i < m_watched.size(); ++i) {
        WatchRow& row = m_watched[i];

        int effectiveCount = natural > 0 ? natural : std::max(1, registerCount);

        if (isPlainInt && row.hasValue) {
            Tag tag;
            if (m_manager.store().getTag(row.nodeId, tag)) {
                const int needed = neededRegisterTier(magnitudeOf(tag.value));
                if (needed > effectiveCount) {
                    effectiveCount = needed;
                    ++oversizeBumpCount;
                }
            }
        }

        ModbusMapping mapping;
        mapping.registerAddress = cursor;
        mapping.registerCount   = effectiveCount;
        mapping.registerType    = registerType;
        mapping.format          = format;

        const std::string collidingId = m_converter->collidingNodeId(row.nodeId, mapping);
        if (!collidingId.empty()) {
            QString otherName = QString::fromStdString(collidingId);
            auto otherIt = m_watchedIndex.find(collidingId);
            if (otherIt != m_watchedIndex.end())
                otherName = QString::fromStdString(m_watched[otherIt->second].logicalName);
            emit logMessage(QStringLiteral("UYARI: '%1' icin %2 adresi '%3' etiketiyle cakisiyor, bu tag atlandi.")
                          .arg(QString::fromStdString(row.logicalName))
                          .arg(cursor)
                          .arg(otherName));
            cursor += effectiveCount;
            ++collisionSkipCount;
            continue;
        }

        applyMapping(row, mapping);
        m_valuesModel->notifyRowChanged(static_cast<int>(i));

        cursor += effectiveCount;
        ++convertedCount;
    }

    QString msg = QStringLiteral("Toplu donusum tamamlandi: %1 tag '%2 / %3' olarak %4 adresinden baslayarak bosluksuz siralandi.")
                      .arg(convertedCount)
                      .arg(QString::fromStdString(formats[static_cast<std::size_t>(formatIndex)].second))
                      .arg(QString::fromStdString(types[static_cast<std::size_t>(registerTypeIndex)].second))
                      .arg(startAddress);
    if (oversizeBumpCount > 0) {
        msg += QStringLiteral(" %1 tag icin deger secilen genisligi astigindan register genisligi otomatik buyutuldu.")
                   .arg(oversizeBumpCount);
    }
    if (collisionSkipCount > 0) {
        msg += QStringLiteral(" %1 tag, baska bir etiketle adres cakismasi nedeniyle atlandi.")
                   .arg(collisionSkipCount);
    }
    emit logMessage(msg);
}

void ModbusController::refreshPreview(WatchRow& row, const TagValue& value) {
    if (!m_converter || !row.hasModbusMapping) {
        row.modbusPreview.clear();
        return;
    }
    const ModbusMapping mapping = m_converter->mappingFor(row.nodeId);
    if (mapping.registerType == ModbusMapping::RegisterType::Coil
        || mapping.registerType == ModbusMapping::RegisterType::DiscreteInput) {
        const char* rt = mapping.registerType == ModbusMapping::RegisterType::Coil ? "CO" : "DI";
        row.modbusPreview = QStringLiteral("[%1] @ %2#%3")
                                .arg(coilBitForPreview(value) ? 1 : 0)
                                .arg(QString::fromUtf8(rt))
                                .arg(mapping.registerAddress)
                                .toStdString();
        return;
    }
    const std::vector<std::uint16_t> regs = m_converter->convert(row.nodeId, value);
    row.modbusPreview = (formatModbusPreview(regs, mapping)
                         + QStringLiteral(" | Q@DI#%1=%2")
                               .arg(mapping.registerAddress)
                               .arg(value.quality == Quality::Good ? 1 : 0))
                            .toStdString();
}

void ModbusController::clearMappingForNode(const std::string& nodeId) {
    if (!m_converter || !m_converter->hasMapping(nodeId)) return;
    const ModbusMapping oldMapping = m_converter->mappingFor(nodeId);
    m_converter->removeMapping(nodeId);
    if (m_sink) m_sink->onMappingRemoved(oldMapping);
}
