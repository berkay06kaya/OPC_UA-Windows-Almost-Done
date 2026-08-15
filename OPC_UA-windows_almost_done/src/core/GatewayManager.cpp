#include "core/GatewayManager.h"

GatewayManager::~GatewayManager() {
    stop();
    for (auto& kv : m_sinkRuntimes) stopSink(kv.first);
}

void GatewayManager::setDataSource(std::unique_ptr<IOpcUaSource> source) {
    m_source = std::move(source);
    if (!m_source) return;

    m_source->setPhaseCallback([this](int phase, std::uint32_t status) {
        if (m_phaseObserver) m_phaseObserver(phase, status);
    });
    m_source->setValueCallback([this](const std::string& nodeId, const TagValue& value) {
        onValue(nodeId, value);
    });
    m_source->setChunkSizeCallback([this](size_t effectiveSize, bool fromServer, std::uint32_t serverDeclaredValue) {
        if (m_chunkSizeObserver) m_chunkSizeObserver(effectiveSize, fromServer, serverDeclaredValue);
    });
}

void GatewayManager::addSink(const std::string& name, std::unique_ptr<IDataSink> sink) {
    if (sink) m_sinks[name] = std::move(sink);
}

bool GatewayManager::startSink(const std::string& name) {
    auto sinkIt = m_sinks.find(name);
    if (sinkIt == m_sinks.end()) return false;

    auto& runtime = m_sinkRuntimes[name];
    if (runtime && runtime->running.load()) return true;

    runtime = std::make_unique<SinkRuntime>();
    runtime->running = true;
    IDataSink* sinkPtr = sinkIt->second.get();
    std::atomic<bool>* runningFlag = &runtime->running;
    runtime->thread = std::thread([sinkPtr, runningFlag] { sinkPtr->run(*runningFlag); });
    return true;
}

void GatewayManager::stopSink(const std::string& name) {
    auto it = m_sinkRuntimes.find(name);
    if (it == m_sinkRuntimes.end() || !it->second) return;

    it->second->running = false;
    if (it->second->thread.joinable()) it->second->thread.join();
}

bool GatewayManager::isSinkRunning(const std::string& name) const {
    auto it = m_sinkRuntimes.find(name);
    return it != m_sinkRuntimes.end() && it->second && it->second->running.load();
}

std::vector<std::string> GatewayManager::converterNames() const {
    std::vector<std::string> names;
    names.reserve(m_converters.size());
    for (auto& kv : m_converters) names.push_back(kv.first);
    return names;
}

bool GatewayManager::start() {
    if (m_running.exchange(true)) return true;

    if (m_source) {
        IOpcUaSource* src = m_source.get();
        m_threads.emplace_back([this, src] { src->run(m_running); });
    }
    return true;
}

void GatewayManager::stop() {
    m_running = false;
    for (auto& t : m_threads)
        if (t.joinable()) t.join();
    m_threads.clear();
}

void GatewayManager::setPhaseObserver(std::function<void(int, std::uint32_t)> cb) {
    m_phaseObserver = std::move(cb);
}

void GatewayManager::setTagObserver(
    std::function<void(const std::string&, const TagValue&)> cb) {
    m_tagObserver = std::move(cb);
}

void GatewayManager::setChunkSizeObserver(
    std::function<void(size_t, bool, std::uint32_t)> cb) {
    m_chunkSizeObserver = std::move(cb);
}

void GatewayManager::onValue(const std::string& nodeId, const TagValue& value) {
    for (auto& kv : m_sinks) kv.second->onTagUpdate(nodeId, value);
    if (m_tagObserver) m_tagObserver(nodeId, value);
}
