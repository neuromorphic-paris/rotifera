#pragma once

#include <thread>
#include <atomic>
#include <stdexcept>

/// EventLoop manages a process listening for events.
class EventLoop {
    public:
        EventLoop() {}
        EventLoop(const EventLoop&) = delete;
        EventLoop(EventLoop&&) = default;
        EventLoop& operator=(const EventLoop&) = delete;
        EventLoop& operator=(EventLoop&&) = default;
        virtual ~EventLoop() {}
};

/// SpecialisedEventLoop is a template-specialised event loop.
template <typename Run, typename HandleException>
class SpecialisedEventLoop : public EventLoop {
    public:
        SpecialisedEventLoop(Run run, HandleException handleException) :
            _run(std::forward<Run>(run)),
            _handleException(std::forward<HandleException>(handleException)),
            _running(true)
        {
            _loop = std::thread([this]() {
                try {
                    this->_run(_running);
                    if (_running.load(std::memory_order_relaxed)) {
                        throw std::logic_error("run returned but running is true");
                    }
                } catch (...) {
                    this->_handleException(std::current_exception());
                }
            });
        }
        SpecialisedEventLoop(const SpecialisedEventLoop&) = delete;
        SpecialisedEventLoop(SpecialisedEventLoop&&) = default;
        SpecialisedEventLoop& operator=(const SpecialisedEventLoop&) = delete;
        SpecialisedEventLoop& operator=(SpecialisedEventLoop&&) = default;
        virtual ~SpecialisedEventLoop() {
            _running.store(false, std::memory_order_relaxed);
            _loop.join();
        }

    protected:
        Run _run;
        HandleException _handleException;
        std::atomic_bool _running;
        std::thread _loop;
};

/// make_eventLoop creates an event loop from functors.
template <typename Run, typename HandleException>
std::unique_ptr<SpecialisedEventLoop<Run, HandleException>> make_eventLoop(Run run, HandleException handleException) {
    return std::unique_ptr<SpecialisedEventLoop<Run, HandleException>>(new SpecialisedEventLoop<Run, HandleException>(
        std::forward<Run>(run),
        std::forward<HandleException>(handleException)
    ));
}
