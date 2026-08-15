#pragma once

#include <atomic>
#include <string>
#include "core/TagValue.h"

class IDataSink {
public:
    virtual ~IDataSink() = default;

    virtual void onTagUpdate(const std::string& nodeId, const TagValue& value) = 0;
    virtual void run(const std::atomic<bool>& running) { (void)running; }
};
