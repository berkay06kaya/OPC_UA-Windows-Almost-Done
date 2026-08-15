#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/IDataSource.h"
#include "core/EndpointInfo.h"
#include "core/BrowseNode.h"
#include "core/TagValue.h"

enum class SourcePhase { Idle = 0, Connecting, Connected, Reconnecting, Failed };

class IOpcUaSource : public IDataSource {
public:
    virtual void setConnectionConfig(const EndpointInfo& endpoint,
                                     const std::string& user,
                                     const std::string& pass) = 0;

    virtual void setPhaseCallback(std::function<void(int phase, std::uint32_t status)> cb) = 0;

    virtual void setValueCallback(
        std::function<void(const std::string& nodeId, const TagValue& value)> cb) = 0;

    virtual void setChunkSizeCallback(
        std::function<void(size_t effectiveSize, bool fromServer, std::uint32_t serverDeclaredValue)> cb) = 0;

    virtual bool isReconnecting() const = 0;
    virtual void setAutoReconnect(bool enabled) = 0;

    virtual std::string lastStatusName() const = 0;
    virtual std::string connectionHint() const = 0;

    virtual void browseChildrenAsync(const std::string& nodeId,
                                     std::function<void(std::vector<BrowseNode>)> cb) = 0;
    virtual std::vector<BrowseNode> browseChildren(const std::string& nodeId) = 0;

    virtual void subscribe(const std::string& nodeId, const std::string& logicalName) = 0;
    virtual void subscribeMany(const std::vector<std::pair<std::string, std::string>>& nodes) = 0;
    virtual void unsubscribe(const std::string& nodeId) = 0;
    virtual void unsubscribeMany(const std::vector<std::string>& nodeIds) = 0;
};
