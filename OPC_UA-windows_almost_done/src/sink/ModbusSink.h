#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "core/IDataSink.h"
#include "core/ModbusMapping.h"
#include "core/TagValue.h"

class DataStore;
class ModbusConverter;

class ModbusSink : public IDataSink {
public:
    ModbusSink(DataStore* dataStore, ModbusConverter* converter);
    ~ModbusSink() override;

    void setPort(int port);
    int port() const;
    void setBindAddress(std::string bindAddress);
    void setUnitId(int unitId);

    void setStatusCallback(std::function<void(bool running, const std::string& msg)> cb);

    void onTagUpdate(const std::string& nodeId, const TagValue& value) override;
    void onMappingRemoved(const ModbusMapping& oldMapping);
    void run(const std::atomic<bool>& running) override;

private:
    struct QueueItem {
        enum class Kind { ValueUpdate, ClearMapping } kind;
        std::string nodeId;
        TagValue value;
        ModbusMapping mapping;
    };

    void syncAllMappings();
    void drainQueue();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    DataStore* m_dataStore;
    ModbusConverter* m_converter;
    int m_port = 1502;
    std::string m_bindAddress;
    int m_unitId = 1;

    std::function<void(bool, const std::string&)> m_statusCb;

    std::mutex m_queueMtx;
    std::deque<QueueItem> m_queue;
};
