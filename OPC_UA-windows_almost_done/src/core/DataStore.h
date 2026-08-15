#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "core/Tag.h"


class DataStore {
public:
    void addTag(const Tag& tag);
    void removeTag(const std::string& nodeId);
    void updateValue(const std::string& nodeId, const TagValue& value);
    bool getTag(const std::string& nodeId, Tag& outTag) const;
    std::vector<Tag> snapshot() const;


    bool renameTag(const std::string& nodeId, const std::string& newLogicalName);

private:
    std::unordered_map<std::string, Tag> m_tags;
    mutable std::mutex m_mutex;
};
