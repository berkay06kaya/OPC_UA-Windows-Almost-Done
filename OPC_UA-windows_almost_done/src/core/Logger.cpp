#include "core/Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace logging {

namespace {

std::mutex g_mutex;
Sink g_sink;

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms;
    return out.str();
}

}

void setSink(Sink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

LogStream::LogStream(bool isError) : m_isError(isError) {}

LogStream::~LogStream() {
    const std::string line = timestamp() + " " + m_stream.str();

    if (m_isError)
        std::cerr << line << std::endl;
    else
        std::cout << line << std::endl;

    Sink sinkCopy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        sinkCopy = g_sink;
    }
    if (sinkCopy) sinkCopy(m_isError, line);
}

}
