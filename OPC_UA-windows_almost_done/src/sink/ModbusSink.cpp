#include "sink/ModbusSink.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <modbus.h>

#ifdef _WIN32
namespace {
int closeSocket(int fd) { return closesocket(static_cast<SOCKET>(fd)); }

std::string wsaReasonText(int wsaErr) {
    switch (wsaErr) {
        case WSAEADDRINUSE: return "Port zaten kullanimda";
        case WSAEADDRNOTAVAIL: return "Bu IP adresi bu makinede tanimli degil";
        case WSAEACCES: return "Erisim reddedildi (yetki/guvenlik duvari)";
        default: return "WSA hata kodu: " + std::to_string(wsaErr);
    }
}
}
#else
namespace {
int closeSocket(int fd) { return close(fd); }
}
#endif

#include "core/DataStore.h"
#include "core/ModbusMapping.h"
#include "core/Tag.h"
#include "sink/ModbusConverter.h"

namespace {

bool isBitTable(ModbusMapping::RegisterType t) {
    return t == ModbusMapping::RegisterType::Coil
        || t == ModbusMapping::RegisterType::DiscreteInput;
}

bool coilBitOf(const TagValue& v) {
    return std::visit([](auto&& x) -> bool {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate>) return false;
        else if constexpr (std::is_same_v<T, bool>) return x;
        else if constexpr (std::is_same_v<T, std::string>)
            return !x.empty() && x != "0" && x != "false" && x != "False";
        else return x != 0;
    }, v.value);
}

void writeWords(modbus_mapping_t* mapping, const ModbusMapping& m, const std::vector<std::uint16_t>& words) {
    std::uint16_t* tab = (m.registerType == ModbusMapping::RegisterType::InputRegister)
                              ? mapping->tab_input_registers
                              : mapping->tab_registers;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const int addr = m.registerAddress + static_cast<int>(i);
        if (addr < 0 || addr >= 65536) continue;
        tab[addr] = words[i];
    }
}

void clearWords(modbus_mapping_t* mapping, const ModbusMapping& m) {
    std::uint16_t* tab = (m.registerType == ModbusMapping::RegisterType::InputRegister)
                              ? mapping->tab_input_registers
                              : mapping->tab_registers;
    const int width = std::max(1, m.registerCount);
    for (int i = 0; i < width; ++i) {
        const int addr = m.registerAddress + i;
        if (addr < 0 || addr >= 65536) continue;
        tab[addr] = 0;
    }
}

void writeBit(modbus_mapping_t* mapping, const ModbusMapping& m, bool bit) {
    const int addr = m.registerAddress;
    if (addr < 0 || addr >= 65536) return;
    std::uint8_t* tab = (m.registerType == ModbusMapping::RegisterType::DiscreteInput)
                              ? mapping->tab_input_bits
                              : mapping->tab_bits;
    tab[addr] = bit ? 1 : 0;
}

void clearBit(modbus_mapping_t* mapping, const ModbusMapping& m) {
    writeBit(mapping, m, false);
}

void writeQualityBit(modbus_mapping_t* mapping, const ModbusMapping& m, Quality q) {
    if (isBitTable(m.registerType)) return;
    const int addr = m.registerAddress;
    if (addr < 0 || addr >= 65536) return;
    mapping->tab_input_bits[addr] = (q == Quality::Good) ? 1 : 0;
}

void clearQualityBit(modbus_mapping_t* mapping, const ModbusMapping& m) {
    if (isBitTable(m.registerType)) return;
    const int addr = m.registerAddress;
    if (addr < 0 || addr >= 65536) return;
    mapping->tab_input_bits[addr] = 0;
}

}

struct ModbusSink::Impl {
    modbus_t* ctx = nullptr;
    modbus_mapping_t* mapping = nullptr;
    int serverSocket = -1;
};

ModbusSink::ModbusSink(DataStore* dataStore, ModbusConverter* converter)
    : m_impl(std::make_unique<Impl>()), m_dataStore(dataStore), m_converter(converter) {}

ModbusSink::~ModbusSink() = default;

void ModbusSink::setPort(int port) { m_port = port; }
int ModbusSink::port() const { return m_port; }
void ModbusSink::setBindAddress(std::string bindAddress) { m_bindAddress = std::move(bindAddress); }
void ModbusSink::setUnitId(int unitId) { m_unitId = unitId; }

void ModbusSink::setStatusCallback(std::function<void(bool, const std::string&)> cb) {
    m_statusCb = std::move(cb);
}

void ModbusSink::onTagUpdate(const std::string& nodeId, const TagValue& value) {
    std::lock_guard<std::mutex> lk(m_queueMtx);
    m_queue.push_back(QueueItem{QueueItem::Kind::ValueUpdate, nodeId, value, {}});
}

void ModbusSink::onMappingRemoved(const ModbusMapping& oldMapping) {
    std::lock_guard<std::mutex> lk(m_queueMtx);
    m_queue.push_back(QueueItem{QueueItem::Kind::ClearMapping, {}, {}, oldMapping});
}

void ModbusSink::syncAllMappings() {
    for (const auto& kv : m_converter->allMappings()) {
        const std::string& nodeId = kv.first;
        const ModbusMapping& mapping = kv.second;
        Tag tag;
        if (!m_dataStore->getTag(nodeId, tag)) continue;
        if (isBitTable(mapping.registerType)) {
            writeBit(m_impl->mapping, mapping, coilBitOf(tag.value));
        } else {
            writeWords(m_impl->mapping, mapping, m_converter->convert(nodeId, tag.value));
            writeQualityBit(m_impl->mapping, mapping, tag.value.quality);
        }
    }
}

void ModbusSink::drainQueue() {
    std::deque<QueueItem> batch;
    {
        std::lock_guard<std::mutex> lk(m_queueMtx);
        std::swap(batch, m_queue);
    }
    for (const QueueItem& item : batch) {
        if (item.kind == QueueItem::Kind::ClearMapping) {
            if (isBitTable(item.mapping.registerType)) {
                clearBit(m_impl->mapping, item.mapping);
            } else {
                clearWords(m_impl->mapping, item.mapping);
                clearQualityBit(m_impl->mapping, item.mapping);
            }
            continue;
        }
        if (!m_converter->hasMapping(item.nodeId)) continue;
        const ModbusMapping mapping = m_converter->mappingFor(item.nodeId);
        if (isBitTable(mapping.registerType)) {
            writeBit(m_impl->mapping, mapping, coilBitOf(item.value));
        } else {
            writeWords(m_impl->mapping, mapping, m_converter->convert(item.nodeId, item.value));
            writeQualityBit(m_impl->mapping, mapping, item.value.quality);
        }
    }
}

void ModbusSink::run(const std::atomic<bool>& running) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    m_impl->ctx = modbus_new_tcp(m_bindAddress.empty() ? nullptr : m_bindAddress.c_str(), m_port);
    if (!m_impl->ctx) {
        if (m_statusCb) m_statusCb(false, "Modbus baglami olusturulamadi");
        return;
    }
    modbus_set_slave(m_impl->ctx, m_unitId);
    modbus_set_indication_timeout(m_impl->ctx, 3, 0);

#ifdef _WIN32
    {
        SOCKET rawSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (rawSocket == INVALID_SOCKET) {
            if (m_statusCb) m_statusCb(false, "Soket olusturulamadi: " + wsaReasonText(WSAGetLastError()));
            modbus_free(m_impl->ctx);
            m_impl->ctx = nullptr;
            return;
        }

        BOOL reuse = TRUE;
        setsockopt(rawSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(m_port));
        if (m_bindAddress.empty()) {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            inet_pton(AF_INET, m_bindAddress.c_str(), &addr.sin_addr);
        }

        if (bind(rawSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            const std::string reason = wsaReasonText(WSAGetLastError());
            closesocket(rawSocket);
            if (m_statusCb) m_statusCb(false, "Port dinlenemedi (bind): " + reason);
            modbus_free(m_impl->ctx);
            m_impl->ctx = nullptr;
            return;
        }
        if (listen(rawSocket, 1) == SOCKET_ERROR) {
            const std::string reason = wsaReasonText(WSAGetLastError());
            closesocket(rawSocket);
            if (m_statusCb) m_statusCb(false, "Port dinlenemedi (listen): " + reason);
            modbus_free(m_impl->ctx);
            m_impl->ctx = nullptr;
            return;
        }
        m_impl->serverSocket = static_cast<int>(rawSocket);
    }
#else
    m_impl->serverSocket = modbus_tcp_listen(m_impl->ctx, 1);
    if (m_impl->serverSocket == -1) {
        const std::string reason = (errno==0)
            ? "IP veya port geçersiz olabilir"
            : modbus_strerror(errno);
        if (m_statusCb) m_statusCb(false, "Port dinlenemedi:" + reason);
        modbus_free(m_impl->ctx);
        m_impl->ctx = nullptr;
        return;
    }
#endif

    m_impl->mapping = modbus_mapping_new_start_address(0, 65536, 0, 65536, 0, 65536, 0, 65536);
    if (!m_impl->mapping) {
        if (m_statusCb) m_statusCb(false, "Register tablosu olusturulamadi");
        closeSocket(m_impl->serverSocket);
        modbus_free(m_impl->ctx);
        m_impl->ctx = nullptr;
        m_impl->serverSocket = -1;
        return;
    }

    syncAllMappings();

    if (m_statusCb)
        m_statusCb(true, "Modbus sunucu " + std::to_string(m_port) + " portunda dinliyor");

    int clientSocket = -1;
    std::uint8_t query[MODBUS_MAX_ADU_LENGTH];

    while (running.load()) {
        fd_set rdset;
        FD_ZERO(&rdset);
        FD_SET(m_impl->serverSocket, &rdset);
        int maxFd = m_impl->serverSocket;
        if (clientSocket != -1) {
            FD_SET(clientSocket, &rdset);
            maxFd = std::max(maxFd, clientSocket);
        }

        timeval tv{0, 100000};
        const int rc = select(maxFd + 1, &rdset, nullptr, nullptr, &tv);

        if (rc > 0) {
            if (FD_ISSET(m_impl->serverSocket, &rdset)) {
                if (clientSocket == -1) {
                    clientSocket = modbus_tcp_accept(m_impl->ctx, &m_impl->serverSocket);
                } else {
                    const int rejected = accept(m_impl->serverSocket, nullptr, nullptr);
                    if (rejected >= 0) closeSocket(rejected);
                }
            }
            if (clientSocket != -1 && FD_ISSET(clientSocket, &rdset)) {
                const int reqLen = modbus_receive(m_impl->ctx, query);
                if (reqLen > 0) {
                    modbus_reply(m_impl->ctx, query, reqLen, m_impl->mapping);
                } else if (reqLen == -1) {
                    closeSocket(clientSocket);
                    clientSocket = -1;
                }
            }
        }

        drainQueue();
    }

    if (clientSocket != -1) closeSocket(clientSocket);
    modbus_mapping_free(m_impl->mapping);
    m_impl->mapping = nullptr;
    closeSocket(m_impl->serverSocket);
    m_impl->serverSocket = -1;
    modbus_free(m_impl->ctx);
    m_impl->ctx = nullptr;
#ifdef _WIN32
    WSACleanup();
#endif

    if (m_statusCb) m_statusCb(false, "Modbus sunucu durduruldu");
}
