#pragma once

#include <fstream>
#include <stdexcept>
#include <chrono>
#include <atomic>
#include <iomanip>

class Log {
    public:
        Log(const std::string& filename) :
            _log(filename)
        {
            if (!_log.good()) {
                throw std::logic_error(filename + " could not be open for writting");
            }
            _writting.clear(std::memory_order_release);
        }
        Log(const Log&) = delete;
        Log(Log&&) = default;
        Log& operator=(const Log&) = delete;
        Log& operator=(Log&&) = default;
        virtual ~Log() {}

        /// write adds an entry to the log system.
        virtual void write(const std::string& message) {
            const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            while (_writting.test_and_set(std::memory_order_acquire)) {}
            _log << "[" << std::put_time(std::localtime(&now), "%F %T") << "] " << message << std::endl;
            _writting.clear(std::memory_order_release);
        }

    protected:
        std::ofstream _log;
        std::atomic_flag _writting;
};
