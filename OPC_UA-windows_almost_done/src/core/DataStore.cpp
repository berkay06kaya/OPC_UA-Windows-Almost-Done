#include "core/DataStore.h"

void DataStore::addTag(const Tag& tag) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tags[tag.nodeId] = tag;
}

void DataStore::removeTag(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tags.erase(nodeId);
}

void DataStore::updateValue(const std::string& nodeId, const TagValue& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tags.find(nodeId);
    if (it != m_tags.end())
        it->second.value = value;
}

bool DataStore::getTag(const std::string& nodeId, Tag& outTag) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tags.find(nodeId);
    if (it != m_tags.end()) {
        outTag = it->second;
        return true;
    }
    return false;
}

std::vector<Tag> DataStore::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Tag> out;
    out.reserve(m_tags.size());
    for (const auto& kv : m_tags)
        out.push_back(kv.second);
    return out;
}

bool DataStore::renameTag(const std::string& nodeId, const std::string& newLogicalName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tags.find(nodeId);
    if (it != m_tags.end()) {
        it->second.logicalName = newLogicalName;
        return true;
    }
    return false;
}