#include "source/OpcUaSubscriber.h"
#include "source/UaUtils.h"
#include "core/DataStore.h"
#include "core/Tag.h"
#include "core/Logger.h"

#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <open62541/client.h>
#include <open62541/client_subscriptions.h>

OpcUaSubscriber::OpcUaSubscriber(DataStore* dataStore) : m_dataStore(dataStore) {}

void OpcUaSubscriber::setValueCallback(
    std::function<void(const std::string&, const TagValue&)> cb) {
    m_valueCallback = std::move(cb);
}

void OpcUaSubscriber::setChunkSizeCallback(
    std::function<void(size_t, bool, std::uint32_t)> cb) {
    m_chunkSizeCallback = std::move(cb);
}

bool OpcUaSubscriber::hasNode(const std::string& nodeId) const {
    for (const auto& kv : m_allNodes)
        if (kv.first == nodeId) return true;
    return false;
}

void OpcUaSubscriber::dataChangeHandler(UA_Client* , UA_UInt32 , void* ,
                                        UA_UInt32 monId, void* monCtx, UA_DataValue* value) {
    auto* self = static_cast<OpcUaSubscriber*>(monCtx);
    if (self == nullptr || value == nullptr) return;

    std::string nodeId;
    auto it = self->m_monIdToNodeId.find(monId);
    if (it != self->m_monIdToNodeId.end()) nodeId = it->second;

    const UA_StatusCode st = value->hasStatus ? value->status : UA_STATUSCODE_GOOD;

    if (st != UA_STATUSCODE_GOOD || !value->hasValue) {
        std::cout << "[SUB][ANOMALI] t=" << ua::nowMs() << "ms nodeId=" << nodeId
                  << " status=" << UA_StatusCode_name(st)
                  << " hasValue=" << (value->hasValue ? "evet" : "hayir") << std::endl;
    }

    TagValue tv;
    if (st == UA_STATUSCODE_GOOD) {
        tv = value->hasValue ? ua::variantToTagValue(value->value) : TagValue{};
        tv.quality = Quality::Good;
        const UA_DateTime ts = value->hasSourceTimestamp ? value->sourceTimestamp : 0;
        tv.timestampMs = ts ? (std::int64_t)((ts - UA_DATETIME_UNIX_EPOCH) / UA_DATETIME_MSEC) : 0;
        self->m_pendingConfirmation.erase(nodeId);
    } else {
        Tag existing;
        if (self->m_dataStore != nullptr && self->m_dataStore->getTag(nodeId, existing))
            tv = existing.value;
        tv.quality = Quality::Bad;
    }

    if (self->m_dataStore != nullptr)
        self->m_dataStore->updateValue(nodeId, tv);

    if (self->m_valueCallback)
        self->m_valueCallback(nodeId, tv);
}

void OpcUaSubscriber::addNode(UA_Client* client, const std::string& nodeId, const std::string& logicalName) {
    if (hasNode(nodeId)) return;
    m_allNodes.emplace_back(nodeId, logicalName);

    if (m_dataStore != nullptr) {
        Tag t;
        t.nodeId = nodeId;
        t.logicalName = logicalName;
        m_dataStore->addTag(t);
    }

    buildWindows();
    if (!m_active) return;

    if (m_subscriptionId == 0 && !createSubscription(client)) return;

    if (m_windows.size() <= 1) {
        createMonitoredItems(client, {nodeId}, m_windows.empty() ? 0 : m_windows[0].id);
    } else if (m_liveNodeIds.empty()) {
        m_activeWindowIndex = 0;
        rotateToWindow(client, 0);
        m_lastRotationMs = ua::nowMs();
    }
}

void OpcUaSubscriber::addNodes(UA_Client* client,
                                const std::vector<std::pair<std::string, std::string>>& nodes) {
    std::vector<std::string> newNodeIds;
    newNodeIds.reserve(nodes.size());

    for (const auto& kv : nodes) {
        const std::string& nodeId = kv.first;
        const std::string& logicalName = kv.second;

        if (hasNode(nodeId)) continue;

        if (m_dataStore != nullptr) {
            Tag t;
            t.nodeId = nodeId;
            t.logicalName = logicalName;
            m_dataStore->addTag(t);
        }
        m_allNodes.emplace_back(nodeId, logicalName);
        newNodeIds.push_back(nodeId);
    }
    if (newNodeIds.empty()) return;

    buildWindows();
    if (!m_active) return;

    if (m_subscriptionId == 0 && !createSubscription(client)) return;

    if (m_windows.size() <= 1) {
        const std::int64_t batchStart = ua::nowMs();
        const std::int64_t serverMs =
            createMonitoredItems(client, newNodeIds, m_windows.empty() ? 0 : m_windows[0].id);
        const std::int64_t totalMs = ua::nowMs() - batchStart;
        LOG_TIMING() << "[SUB][TIMING] toplu abone: " << newNodeIds.size() << " tag server="
                     << serverMs << "ms kod=" << (totalMs - serverMs) << "ms toplam=" << totalMs << "ms";
    } else if (m_liveNodeIds.empty()) {
        m_activeWindowIndex = 0;
        rotateToWindow(client, 0);
        m_lastRotationMs = ua::nowMs();
    }
}

void OpcUaSubscriber::removeNode(UA_Client* client, const std::string& nodeId) {
    removeNodes(client, {nodeId});
}

void OpcUaSubscriber::removeNodes(UA_Client* client, const std::vector<std::string>& nodeIds) {
    if (nodeIds.empty()) return;

    std::unordered_set<std::string> toRemove(nodeIds.begin(), nodeIds.end());

    m_allNodes.erase(
        std::remove_if(m_allNodes.begin(), m_allNodes.end(),
                        [&](const auto& kv) { return toRemove.count(kv.first) != 0; }),
        m_allNodes.end());

    for (const auto& nodeId : nodeIds) m_pendingConfirmation.erase(nodeId);

    buildWindows();

    if (m_active && client != nullptr) {
        std::vector<std::string> live;
        live.reserve(nodeIds.size());
        for (const auto& nodeId : nodeIds) {
            if (std::find(m_liveNodeIds.begin(), m_liveNodeIds.end(), nodeId) != m_liveNodeIds.end())
                live.push_back(nodeId);
        }
        if (!live.empty()) {
            deleteMonitoredItems(client, live);
            if (live.size() == 1)
                std::cout << "[SUB] izleme birakildi: " << live[0] << std::endl;
            else
                std::cout << "[SUB] toplu izleme birakildi: " << live.size() << " tag" << std::endl;
        }
    }

    if (m_dataStore != nullptr)
        for (const auto& nodeId : nodeIds) m_dataStore->removeTag(nodeId);
}

bool OpcUaSubscriber::createSubscription(UA_Client* client) {
    UA_CreateSubscriptionRequest req = UA_CreateSubscriptionRequest_default();
    req.requestedPublishingInterval = 200.0;
    const std::int64_t rpcStart = ua::nowMs();
    UA_CreateSubscriptionResponse resp =
        UA_Client_Subscriptions_create(client, req, this, nullptr, nullptr);
    LOG_TIMING() << "[SUB][TIMING] server RPC (createSubscription): "
                 << (ua::nowMs() - rpcStart) << "ms";
    if (resp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        LOG_ERROR() << "[SUB] abonelik kurulamadi: "
                    << UA_StatusCode_name(resp.responseHeader.serviceResult);
        return false;
    }
    m_subscriptionId = resp.subscriptionId;
    m_monIdToNodeId.clear();
    m_liveNodeIds.clear();
    std::cout << "[SUB] abonelik kuruldu (id=" << m_subscriptionId << "). revised: "
              << "publishingInterval=" << resp.revisedPublishingInterval << "ms "
              << "lifetimeCount=" << resp.revisedLifetimeCount << " "
              << "maxKeepAliveCount=" << resp.revisedMaxKeepAliveCount << std::endl;
    return true;
}

void OpcUaSubscriber::onConnected(UA_Client* client) {
    if (client == nullptr) return;

    buildWindows();

    if (!m_windows.empty() && createSubscription(client)) {
        const Window& active = m_windows[m_activeWindowIndex];
        createMonitoredItems(client, active.nodeIds, active.id);
        m_pendingConfirmation = std::set<std::string>(m_liveNodeIds.begin(), m_liveNodeIds.end());
    }

    m_lastRotationMs = ua::nowMs();
    m_active = true;
}

void OpcUaSubscriber::buildWindows() {
    m_windows.clear();
    const size_t total = m_allNodes.size();
    if (total == 0) return;

    std::uint32_t nextId = 0;
    for (size_t start = 0; start < total; start += kWindowSize) {
        Window w;
        w.id = nextId++;
        const size_t count = std::min(kWindowSize, total - start);
        w.nodeIds.reserve(count);
        for (size_t i = 0; i < count; ++i)
            w.nodeIds.push_back(m_allNodes[start + i].first);
        m_windows.push_back(std::move(w));
    }

    if (m_activeWindowIndex >= m_windows.size()) m_activeWindowIndex = 0;
}

void OpcUaSubscriber::tick(UA_Client* client) {
    if (!m_active || client == nullptr || m_subscriptionId == 0) return;
    if (m_windows.size() <= 1) return;

    const std::int64_t elapsed = ua::nowMs() - m_lastRotationMs;
    const bool ceilingReached = elapsed >= kDwellMs;
    const bool dataConfirmed = m_pendingConfirmation.empty();
    if (!ceilingReached && !dataConfirmed) return;

    const char* waitReason = dataConfirmed ? "veri_onaylandi" : "tavan_doldu";

    m_activeWindowIndex = (m_activeWindowIndex + 1) % m_windows.size();
    m_lastRotationMs = ua::nowMs();

    rotateToWindow(client, m_activeWindowIndex, elapsed, waitReason);
}

void OpcUaSubscriber::rotateToWindow(UA_Client* client, size_t windowIndex,
                                      std::int64_t waitMs, const char* waitReason) {
    const Window& target = m_windows[windowIndex];
    const std::int64_t rotationStart = ua::nowMs();

    std::vector<std::string> leaving;
    for (const auto& nodeId : m_liveNodeIds) {
        if (std::find(target.nodeIds.begin(), target.nodeIds.end(), nodeId) == target.nodeIds.end())
            leaving.push_back(nodeId);
    }
    std::vector<std::string> entering;
    for (const auto& nodeId : target.nodeIds) {
        if (std::find(m_liveNodeIds.begin(), m_liveNodeIds.end(), nodeId) == m_liveNodeIds.end())
            entering.push_back(nodeId);
    }

    const std::int64_t rpcMs = deleteMonitoredItems(client, leaving)
                              + createMonitoredItems(client, entering, target.id);

    m_pendingConfirmation = std::set<std::string>(m_liveNodeIds.begin(), m_liveNodeIds.end());

    // switchMs: MonitoredItems RPC'leri (network+server) ile bizim liste hesaplama/response
    // isleme surelerimizin toplami. codeMs: switchMs icindeki RPC disi (bizim) kisim.
    const std::int64_t switchMs = ua::nowMs() - rotationStart;
    const std::int64_t codeMs = switchMs - rpcMs;

    // cycleMs: bir onceki rotasyondan bu rotasyonun tamamlanmasina kadarki tam dongu.
    // serverMs: bu dongude server'a atfedilebilecek toplam sure (publish bekleme + RPC).
    const std::int64_t cycleMs = waitMs + switchMs;
    const std::int64_t serverMs = waitMs + rpcMs;
    const std::int64_t serverPct = cycleMs > 0 ? (100 * serverMs / cycleMs) : 0;

    LOG_TIMING() << "[SUB][TIMING] ROTASYON DONGUSU windowId=" << target.id
                 << " leaving=" << leaving.size() << " entering=" << entering.size()
                 << " tetiklenmeSebebi=" << waitReason
                 << " | subscriptionPublishBekleme=" << waitMs
                 << "ms (server'in yeni pencerenin tum tag'lerini yayinlamasi icin gecen sure)"
                 << " | monitoredItemsRpc=" << rpcMs
                 << "ms (delete+create MonitoredItems servis cagrisi: network+server islem suresi)"
                 << " | kodSuremiz=" << codeMs
                 << "ms (bizim liste hesaplama + response isleme suremiz)"
                 << " | donguToplam=" << cycleMs << "ms"
                 << " | serverPayi=%" << serverPct;
}

std::int64_t OpcUaSubscriber::createMonitoredItems(UA_Client* client, const std::vector<std::string>& nodeIds,
                                                    std::uint32_t windowId) {
    if (client == nullptr || m_subscriptionId == 0 || nodeIds.empty()) return 0;

    std::vector<UA_MonitoredItemCreateRequest> items;
    std::vector<std::string> validNodeIds;
    items.reserve(nodeIds.size());
    validNodeIds.reserve(nodeIds.size());

    for (const auto& nodeId : nodeIds) {
        UA_NodeId id;
        UA_NodeId_init(&id);
        if (UA_NodeId_parse(&id, UA_STRING_ALLOC(nodeId.c_str())) != UA_STATUSCODE_GOOD) {
            LOG_ERROR() << "[SUB] gecersiz nodeId: " << nodeId;
            continue;
        }
        items.push_back(UA_MonitoredItemCreateRequest_default(id));
        validNodeIds.push_back(nodeId);
    }
    if (items.empty()) return 0;

    UA_CreateMonitoredItemsRequest req;
    UA_CreateMonitoredItemsRequest_init(&req);
    req.subscriptionId = m_subscriptionId;
    req.timestampsToReturn = UA_TIMESTAMPSTORETURN_SOURCE;
    req.itemsToCreate = items.data();
    req.itemsToCreateSize = items.size();

    std::vector<void*> contexts(items.size(), this);
    std::vector<UA_Client_DataChangeNotificationCallback> callbacks(
        items.size(), &OpcUaSubscriber::dataChangeHandler);
    std::vector<UA_Client_DeleteMonitoredItemCallback> deleteCallbacks(items.size(), nullptr);

    const std::int64_t rpcStart = ua::nowMs();
    UA_CreateMonitoredItemsResponse resp = UA_Client_MonitoredItems_createDataChanges(
        client, req, contexts.data(), callbacks.data(), deleteCallbacks.data());
    const std::int64_t rpcMs = ua::nowMs() - rpcStart;

    for (size_t i = 0; i < resp.resultsSize && i < validNodeIds.size(); ++i) {
        const UA_MonitoredItemCreateResult& res = resp.results[i];
        const std::string& nodeId = validNodeIds[i];
        if (res.statusCode == UA_STATUSCODE_GOOD) {
            m_monIdToNodeId[res.monitoredItemId] = nodeId;
            m_monIdToWindowId[res.monitoredItemId] = windowId;
            m_liveNodeIds.push_back(nodeId);
            std::cout << "[SUB] izlemeye alindi: " << nodeId
                      << " (monId=" << res.monitoredItemId << ", windowId=" << windowId << ")" << std::endl;
        } else {
            LOG_ERROR() << "[SUB] izleme eklenemedi (" << nodeId << "): "
                        << UA_StatusCode_name(res.statusCode);
        }
    }

    UA_CreateMonitoredItemsResponse_clear(&resp);
    for (auto& item : items) UA_MonitoredItemCreateRequest_clear(&item);
    return rpcMs;
}

std::int64_t OpcUaSubscriber::deleteMonitoredItems(UA_Client* client, const std::vector<std::string>& nodeIds) {
    if (client == nullptr || m_subscriptionId == 0 || nodeIds.empty()) return 0;

    std::vector<UA_UInt32> monIds;
    monIds.reserve(nodeIds.size());
    for (const auto& nodeId : nodeIds) {
        for (const auto& kv : m_monIdToNodeId) {
            if (kv.second == nodeId) { monIds.push_back(kv.first); break; }
        }
    }
    if (monIds.empty()) return 0;

    UA_DeleteMonitoredItemsRequest req;
    UA_DeleteMonitoredItemsRequest_init(&req);
    req.subscriptionId = m_subscriptionId;
    req.monitoredItemIds = monIds.data();
    req.monitoredItemIdsSize = monIds.size();

    const std::int64_t rpcStart = ua::nowMs();
    UA_DeleteMonitoredItemsResponse resp = UA_Client_MonitoredItems_delete(client, req);
    const std::int64_t rpcMs = ua::nowMs() - rpcStart;
    UA_DeleteMonitoredItemsResponse_clear(&resp);

    for (UA_UInt32 monId : monIds) {
        m_monIdToNodeId.erase(monId);
        m_monIdToWindowId.erase(monId);
    }
    m_liveNodeIds.erase(
        std::remove_if(m_liveNodeIds.begin(), m_liveNodeIds.end(),
                        [&](const std::string& n) {
                            return std::find(nodeIds.begin(), nodeIds.end(), n) != nodeIds.end();
                        }),
        m_liveNodeIds.end());
    return rpcMs;
}

void OpcUaSubscriber::onDisconnected() {
    m_subscriptionId = 0;
    m_monIdToNodeId.clear();
    m_monIdToWindowId.clear();
    m_liveNodeIds.clear();
    m_windows.clear();
    m_activeWindowIndex = 0;
    m_pendingConfirmation.clear();
    m_active = false;
}

void OpcUaSubscriber::shutdown(UA_Client* client) {
    if (client != nullptr && !m_liveNodeIds.empty()) {
        std::cout << "[SUB] kapanis oncesi kalan izlemeler birakiliyor: "
                  << m_liveNodeIds.size() << " tag" << std::endl;
        deleteMonitoredItems(client, m_liveNodeIds);
    }
    m_allNodes.clear();
    onDisconnected();
}
