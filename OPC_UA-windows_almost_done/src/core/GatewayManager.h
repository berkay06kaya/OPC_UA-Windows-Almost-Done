#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <atomic>
#include <thread>

#include "core/DataStore.h"
#include "core/IOpcUaSource.h"
#include "core/IDataSink.h"
#include "core/IConverter.h"
#include "core/TagValue.h"

class IConverterSlot {
public:
    virtual ~IConverterSlot() = default;
};

template <typename Out>
class ConverterSlot : public IConverterSlot {
public:
    explicit ConverterSlot(std::unique_ptr<IConverter<Out>> c) : converter(std::move(c)) {}
    std::unique_ptr<IConverter<Out>> converter;
};

class GatewayManager {
public:
    GatewayManager() = default;
    ~GatewayManager();

    GatewayManager(const GatewayManager&) = delete;
    GatewayManager& operator=(const GatewayManager&) = delete;

    void setDataSource(std::unique_ptr<IOpcUaSource> source);
    void addSink(const std::string& name, std::unique_ptr<IDataSink> sink);

    template <typename Out>
    void setConverter(const std::string& name, std::unique_ptr<IConverter<Out>> converter) {
        m_converters[name] = std::make_unique<ConverterSlot<Out>>(std::move(converter));
    }

    template <typename Out>
    IConverter<Out>* converter(const std::string& name) const {
        auto it = m_converters.find(name);
        if (it == m_converters.end()) return nullptr;
        auto* slot = dynamic_cast<ConverterSlot<Out>*>(it->second.get());
        return slot ? slot->converter.get() : nullptr;
    }

    std::vector<std::string> converterNames() const;

    template <typename T>
    T* sink(const std::string& name) const {
        auto it = m_sinks.find(name);
        if (it == m_sinks.end()) return nullptr;
        return dynamic_cast<T*>(it->second.get());
    }

    bool startSink(const std::string& name);
    void stopSink(const std::string& name);
    bool isSinkRunning(const std::string& name) const;

    bool start();
    void stop();

    DataStore& store() { return m_store; }
    IOpcUaSource* source() { return m_source.get(); }

    void setPhaseObserver(std::function<void(int phase, std::uint32_t status)> cb);
    void setTagObserver(std::function<void(const std::string& nodeId, const TagValue& value)> cb);
    void setChunkSizeObserver(
        std::function<void(size_t effectiveSize, bool fromServer, std::uint32_t serverDeclaredValue)> cb);

private:
    struct SinkRuntime {
        std::atomic<bool> running{false};
        std::thread thread;
    };

    void onValue(const std::string& nodeId, const TagValue& value);

    DataStore m_store;
    std::unique_ptr<IOpcUaSource> m_source;

    std::unordered_map<std::string, std::unique_ptr<IConverterSlot>> m_converters;
    std::unordered_map<std::string, std::unique_ptr<IDataSink>> m_sinks;
    std::unordered_map<std::string, std::unique_ptr<SinkRuntime>> m_sinkRuntimes;

    std::atomic<bool> m_running{false};
    std::vector<std::thread> m_threads;

    std::function<void(int, std::uint32_t)> m_phaseObserver;
    std::function<void(const std::string&, const TagValue&)> m_tagObserver;
    std::function<void(size_t, bool, std::uint32_t)> m_chunkSizeObserver;
};
