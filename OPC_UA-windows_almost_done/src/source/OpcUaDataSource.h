#pragma once

#include "core/IOpcUaSource.h"
#include "core/DataStore.h"
#include "core/EndpointInfo.h"
#include "core/BrowseNode.h"
#include "core/TagValue.h"
#include "source/OpcUaBrowser.h"
#include <string>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>
#include <future>
#include <deque>

struct UA_Client;
class OpcUaSubscriber;

class OpcUaDataSource : public IOpcUaSource {
public:
    enum Phase { Idle, Connecting, Connected, Reconnecting, Failed };

private:
    EndpointInfo m_targetEndpoint;
    std::string m_username;
    std::string m_password;

    std::atomic<int> m_phase{Idle};
    std::atomic<std::uint32_t> m_lastStatusCode{0};
    bool m_autoReconnect;

    std::chrono::steady_clock::time_point m_lastReconnectAttempt;
    static constexpr std::chrono::seconds RECONNECT_INTERVAL{3};

    std::chrono::steady_clock::time_point m_connectingSince;
    static constexpr std::chrono::seconds CONNECT_TIMEOUT{15};

    std::chrono::steady_clock::time_point m_lastSubscribeAttempt;
    static constexpr std::chrono::seconds SUBSCRIBE_RETRY_INTERVAL{3};

    UA_Client* m_client;
    DataStore* m_dataStore;

    std::function<void(int phase, std::uint32_t status)> m_phaseCallback;

    struct BrowseRequest {
        std::string nodeId;
        std::function<void(std::vector<BrowseNode>)> done;
    };
    std::mutex m_browseMutex;
    std::deque<BrowseRequest> m_browseQueue;

    std::mutex m_taskMutex;
    std::deque<std::function<void()>> m_taskQueue;

    OpcUaBrowser m_browser;
    std::unique_ptr<OpcUaSubscriber> m_subscriber;

    void cleanupClient();
    void setPhase(Phase p);
    bool connectClient();
    void postToWorker(std::function<void()> task);

    friend struct OpcUaDataSourceInternal;

public:
    explicit OpcUaDataSource(DataStore* dataStore);
    ~OpcUaDataSource() override;

    void setConnectionConfig(const EndpointInfo& endpoint, const std::string& user, const std::string& pass) override;
    void setPhaseCallback(std::function<void(int phase, std::uint32_t status)> cb) override;

    void run(const std::atomic<bool>& running) override;
    bool isConnected() const override;
    bool isReconnecting() const override;

    Phase phase() const { return static_cast<Phase>(m_phase.load()); }
    std::string lastStatusName() const override;
    std::string connectionHint() const override;

    void setAutoReconnect(bool enabled) override;

    void browseChildrenAsync(const std::string& nodeId,
                             std::function<void(std::vector<BrowseNode>)> cb) override;
    std::vector<BrowseNode> browseChildren(const std::string& nodeId) override;

    void subscribe(const std::string& nodeId, const std::string& logicalName) override;
    void subscribeMany(const std::vector<std::pair<std::string, std::string>>& nodes) override;

    void unsubscribe(const std::string& nodeId) override;
    void unsubscribeMany(const std::vector<std::string>& nodeIds) override;

    void setValueCallback(
        std::function<void(const std::string& nodeId, const TagValue& value)> cb) override;

    void setChunkSizeCallback(
        std::function<void(size_t effectiveSize, bool fromServer, std::uint32_t serverDeclaredValue)> cb) override;
};
