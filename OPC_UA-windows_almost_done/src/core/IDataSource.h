#pragma once

#include <atomic>

class IDataSource {
public:
    virtual ~IDataSource() = default;

    virtual void run(const std::atomic<bool>& running) = 0;
    virtual bool isConnected() const = 0;
};
