#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "core/IConverter.h"
#include "core/ModbusMapping.h"

class ModbusConverter : public IConverter<std::vector<std::uint16_t>> {
public:
    std::vector<std::uint16_t> convert(const std::string& nodeId, const TagValue& in) const override;

    void setMapping(const std::string& nodeId, const ModbusMapping& mapping);
    void removeMapping(const std::string& nodeId);
    bool hasMapping(const std::string& nodeId) const;
    ModbusMapping mappingFor(const std::string& nodeId) const;
    std::string collidingNodeId(const std::string& nodeId, const ModbusMapping& mapping) const;
    std::unordered_map<std::string, ModbusMapping> allMappings() const;

private:
    std::unordered_map<std::string, ModbusMapping> m_mappings;
    mutable std::mutex m_mtx;
};
