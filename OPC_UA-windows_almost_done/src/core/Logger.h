#pragma once

#include <functional>
#include <sstream>
#include <string>

namespace logging {

using Sink = std::function<void(bool isError, const std::string& line)>;

void setSink(Sink sink);

class LogStream {
public:
    explicit LogStream(bool isError);
    ~LogStream();

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;

    template <typename T>
    LogStream& operator<<(const T& value) {
        m_stream << value;
        return *this;
    }

private:
    bool m_isError;
    std::ostringstream m_stream;
};

}

#define LOG_ERROR()  ::logging::LogStream(true)
#define LOG_TIMING() ::logging::LogStream(false)
