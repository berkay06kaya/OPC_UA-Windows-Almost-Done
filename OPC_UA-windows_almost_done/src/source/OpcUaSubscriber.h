#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include <functional>
#include <utility>
#include <open62541/types.h>
#include "core/TagValue.h"

struct UA_Client;
class DataStore;

class OpcUaSubscriber {
public:
    explicit OpcUaSubscriber(DataStore* dataStore);

    void setValueCallback(
        std::function<void(const std::string& nodeId, const TagValue& value)> cb);

    void setChunkSizeCallback(
        std::function<void(size_t effectiveSize, bool fromServer, std::uint32_t serverDeclaredValue)> cb);

    void addNode(UA_Client* client, const std::string& nodeId, const std::string& logicalName);
    void addNodes(UA_Client* client, const std::vector<std::pair<std::string, std::string>>& nodes);

    void removeNode(UA_Client* client, const std::string& nodeId);
    void removeNodes(UA_Client* client, const std::vector<std::string>& nodeIds);
    void onConnected(UA_Client* client);
    void onDisconnected();
    void shutdown(UA_Client* client);

    void tick(UA_Client* client);

    bool isActive() const { return m_active; }

private:
    struct Window {
        std::uint32_t id;
        std::vector<std::string> nodeIds;
    };

    bool createSubscription(UA_Client* client);
    // Donen deger: sadece server RPC cagrisinin surdugu ms (bizim islem suremiz haric).
    std::int64_t createMonitoredItems(UA_Client* client, const std::vector<std::string>& nodeIds,
                                       std::uint32_t windowId);
    std::int64_t deleteMonitoredItems(UA_Client* client, const std::vector<std::string>& nodeIds);
    void buildWindows();
    bool hasNode(const std::string& nodeId) const;

    // waitMs/waitReason: rotasyonun tetiklenmesinden once subscription'in yeni pencerenin
    // tum tag'lerini publish etmesini beklerken gecen sure (server veri yayin gecikmesi).
    void rotateToWindow(UA_Client* client, size_t windowIndex,
                         std::int64_t waitMs = 0, const char* waitReason = "ilk_abone");

    static void dataChangeHandler(UA_Client* client, UA_UInt32 subId, void* subCtx,
                                  UA_UInt32 monId, void* monCtx, UA_DataValue* value);

    DataStore* m_dataStore;
    std::function<void(const std::string&, const TagValue&)> m_valueCallback;
    std::function<void(size_t, bool, std::uint32_t)> m_chunkSizeCallback;

    bool m_active = false;
    UA_UInt32 m_subscriptionId = 0;
    std::vector<std::pair<std::string, std::string>> m_allNodes;
    std::vector<std::string> m_liveNodeIds;
    std::map<UA_UInt32, std::string> m_monIdToNodeId;
    std::map<UA_UInt32, std::uint32_t> m_monIdToWindowId;

    std::vector<Window> m_windows;
    size_t m_activeWindowIndex = 0;
    std::int64_t m_lastRotationMs = 0;
    std::set<std::string> m_pendingConfirmation;

    static constexpr size_t kWindowSize = 60;
    static constexpr std::int64_t kDwellMs = 2000;
};
