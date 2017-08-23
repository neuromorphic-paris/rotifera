#include "eventLoop.hpp"
#include "tty.hpp"

#include <sys/un.h>
#include <sys/stat.h>

#include <chrono>
#include <vector>
#include <array>
#include <mutex>
#include <condition_variable>
#include <iostream>

/// Control determines which remote is controlling the buggy.
enum class Control {
    base, // the radio module driven by the developper to implement custom behavior
    radio, // the original buggy controller, required as a security (the buggy will stop if the controller is not switched on)
    lost, // connection with the radio controller lost, stops the buggy until the connection is back
};

/// motorsZeros defines the neutral command for each motor.
const auto motorsZeros = std::array<uint16_t, 2>{
    1500, // direction
    1552, // throttle
};

int main() {
    try {
        std::exception_ptr exception;
        std::mutex exceptionLock;
        std::condition_variable exceptionChanged;
        auto handleException = [&](std::exception_ptr loopException) {
            {
                std::lock_guard<std::mutex> lockGuard(exceptionLock);
                if (!exception) {
                    exception = loopException;
                }
            }
            exceptionChanged.notify_one();
        };
        {
            // common state
            std::atomic<Control> control{Control::base};
            auto arduino = Tty("/dev/ttyACM0", B230400, 10);
            auto base = Tty("/dev/ttyUSB0", B57600, 1);
            std::vector<std::pair<uint8_t, uint16_t>> indicesAndValues;
            std::vector<std::pair<uint8_t, uint16_t>> bufferedIndicesAndValues;
            std::mutex indicesAndValuesLock;
            std::condition_variable indicesAndValuesChanged;
            std::vector<int32_t> sockets;
            std::mutex socketsLock;

            // destruction utilities
            std::unique_lock<std::mutex> uniqueLock(exceptionLock);
            std::vector<std::unique_ptr<EventLoop>> eventLoops{};

            // handle motor commands
            eventLoops.push_back(make_eventLoop([&](std::atomic_bool& running) {
                while (running.load(std::memory_order_relaxed)) {
                    {
                        std::unique_lock<std::mutex> uniqueLock(indicesAndValuesLock);
                        if (indicesAndValuesChanged.wait_for(uniqueLock, std::chrono::milliseconds(100)) == std::cv_status::no_timeout) {
                            bufferedIndicesAndValues.swap(indicesAndValues);
                        }
                    }
                    for (auto indexAndValue : bufferedIndicesAndValues) {

                        //             | LSB  | bit 1 | bit 2 | bit 3 | bit 4 | bit 5 | bit 6 | MSB
                        // ------------|------|-------|-------|-------|-------|-------|-------|-------
                        // First byte  | 0    | 0     | i[0]  | i[1]  | i[2]  | i[3]  | i[4]  | i[5]
                        // Second byte | 1    | 0     | v[0]  | v[1]  | v[2]  | v[3]  | v[4]  | v[5]
                        // Third byte  | 0    | 1     | v[6]  | v[7]  | v[8]  | v[9]  | v[10] | v[11]
                        arduino.write(std::vector<uint8_t>{
                            static_cast<uint8_t>(0b00 | (indexAndValue.first << 2)),
                            static_cast<uint8_t>(0b01 | (indexAndValue.second << 2)),
                            static_cast<uint8_t>(0b10 | ((indexAndValue.second >> 4) & 0xfc)),
                        });
                    }
                }
            }, handleException));

            // listen to the radio controller stream
            eventLoops.push_back(make_eventLoop([&](std::atomic_bool& running) {
                auto previousBytes = std::array<uint8_t, 2>{};
                uint8_t expectedByteId = 0;
                std::size_t badCounter = 0;
                std::size_t onlyOnesCounter = 0;
                std::size_t goodCounter = 0;
                auto preemptCounters = std::array<uint64_t, motorsZeros.size()>{0, 0};
                while (running.load(std::memory_order_relaxed)) {
                    try {
                        const auto byte = arduino.read();
                        if ((byte & 0b11) != expectedByteId) {
                            expectedByteId = 0;
                        } else if (expectedByteId < 2) {
                            previousBytes[expectedByteId] = byte;
                            ++expectedByteId;
                        } else {
                            expectedByteId = 0;
                            const uint8_t index = (previousBytes[0] >> 2);
                            if (index >= motorsZeros.size()) {
                                throw std::logic_error("the arduino sent an out-of-range index");
                            }
                            const uint16_t value = static_cast<uint16_t>(previousBytes[1] >> 2) | (static_cast<uint16_t>(byte & 0xfc) << 4);
                            switch (control.load(std::memory_order_acquire)) {
                                case Control::base: {
                                    if (value < 800 || value > 2200) {
                                        ++badCounter;
                                        if (badCounter > 10) {
                                            throw std::runtime_error("bad values");
                                        }
                                    } else if (std::abs(value - motorsZeros[index]) > 100) {
                                        ++preemptCounters[index];
                                        if (preemptCounters[index] > 10) {
                                            control.store(Control::radio, std::memory_order_release);
                                        }
                                    } else {
                                        preemptCounters[index] = 0;
                                        if (index == 0) {
                                            onlyOnesCounter = 0;
                                        } else {
                                            ++onlyOnesCounter;
                                            if (onlyOnesCounter > 10) {
                                                throw std::runtime_error("only ones");
                                            }
                                        }
                                    }
                                    break;
                                }
                                case Control::radio: {
                                    for (auto& preemptCounter : preemptCounters) {
                                        preemptCounter = 0;
                                    }
                                    if (value < 800 || value > 2200) {
                                        ++badCounter;
                                        if (badCounter > 10) {
                                            throw std::runtime_error("bad values");
                                        }
                                    } else {
                                        if (index == 0) {
                                            onlyOnesCounter = 0;
                                        } else {
                                            ++onlyOnesCounter;
                                            if (onlyOnesCounter > 10) {
                                                throw std::runtime_error("only ones");
                                            }
                                        }
                                        {
                                            std::lock_guard<std::mutex> lockGuard(indicesAndValuesLock);
                                            indicesAndValues.emplace_back(index, value);
                                        }
                                        indicesAndValuesChanged.notify_one();
                                    }
                                    break;
                                }
                                case Control::lost: {
                                    if (value > 800 && value < 2200) {
                                        ++goodCounter;
                                        if (goodCounter > 10) {
                                            goodCounter = 0;
                                            control.store(Control::radio, std::memory_order_release);
                                        }
                                    } else {
                                        goodCounter = 0;
                                    }
                                    break;
                                }
                            }
                        }
                    } catch (const std::runtime_error& exception) {
                        badCounter = 0;
                        goodCounter = 0;
                        for (auto& preemptCounter : preemptCounters) {
                            preemptCounter = 0;
                        }
                        control.store(Control::lost, std::memory_order_release);
                        {
                            std::lock_guard<std::mutex> lockGuard(indicesAndValuesLock);
                            indicesAndValues.clear();
                            indicesAndValues.emplace_back(1, std::get<1>(motorsZeros));
                        }
                        indicesAndValuesChanged.notify_one();
                    }
                }
            }, handleException));

            // listen to the base events
            eventLoops.push_back(make_eventLoop([&](std::atomic_bool& running) {
                auto message = std::vector<uint8_t>{};
                auto readingMessage = false;
                auto escapedCharacter = false;
                auto specialMessage = false;
                uint64_t specialMessageId = 0;
                while (running.load(std::memory_order_relaxed)) {
                    try {
                        const auto byte = base.read();
                        if (readingMessage) {
                            switch (byte) {
                                case 0x00: {
                                    message.clear();
                                    escapedCharacter = false;
                                    specialMessage = false;
                                    break;
                                }
                                case 0xaa: {
                                    escapedCharacter = true;
                                    break;
                                }
                                case 0xff: {
                                    readingMessage = false;
                                    if (!escapedCharacter) {
                                        if (specialMessage) {
                                            switch (specialMessageId) {
                                                case 0: {
                                                    if (control.load(std::memory_order_release) != Control::lost) {
                                                        control.store(Control::base, std::memory_order_release);
                                                    }
                                                    break;
                                                }
                                                case 1: {
                                                    if (control.load(std::memory_order_release) != Control::lost) {
                                                        control.store(Control::radio, std::memory_order_release);
                                                    }
                                                    break;
                                                }
                                                case 2: {

                                                    // prepare the message
                                                    auto message = std::vector<uint8_t>{};
                                                    switch (control.load(std::memory_order_relaxed)) {
                                                        case Control::base: {
                                                            message.push_back(0x00);
                                                        }
                                                        case Control::radio: {
                                                            message.push_back(0x01);
                                                        }
                                                        case Control::lost: {
                                                            message.push_back(0x02);
                                                        }
                                                    }

                                                    // encode and send the message
                                                    auto bytes = std::vector<uint8_t>{0x00};
                                                    for (auto byte : message) {
                                                        switch (byte) {
                                                            case 0x00: {
                                                                bytes.push_back(0xaa);
                                                                bytes.push_back(0xab);
                                                                break;
                                                            }
                                                            case 0xaa: {
                                                                bytes.push_back(0xaa);
                                                                bytes.push_back(0xac);
                                                                break;
                                                            }
                                                            case 0xff: {
                                                                bytes.push_back(0xaa);
                                                                bytes.push_back(0xad);
                                                                break;
                                                            }
                                                            default: {
                                                                bytes.push_back(byte);
                                                            }
                                                        }
                                                    }
                                                    bytes.push_back(0xff);
                                                    base.write(bytes);
                                                    break;
                                                }
                                                default: {
                                                    throw std::logic_error("unknown special message id");
                                                }
                                            }
                                        } else if (message.size() > 1 && message.size() <= 4097) {
                                            message.resize(message.size() - 1);
                                            std::lock_guard<std::mutex> lockGuard(socketsLock);
                                            for (auto socketIterator = sockets.begin(); socketIterator != sockets.end();) {
                                                if (send(*socketIterator, message.data(), message.size(), MSG_NOSIGNAL) <= 0) {
                                                    socketIterator = sockets.erase(socketIterator);
                                                } else {
                                                    ++socketIterator;
                                                }
                                            }

                                            // @DEBUG {
                                            std::cout << "Message received: {";
                                            for (auto byte : message) {
                                                std::cout << +byte << ", ";
                                            }
                                            std::cout << "}" << std::endl;
                                            // }
                                        }
                                    }
                                    break;
                                }
                                default: {
                                    if (escapedCharacter) {
                                        escapedCharacter = false;
                                        switch (byte) {
                                            case 0xab: {
                                                message.push_back(0x00);
                                                break;
                                            }
                                            case 0xac: {
                                                message.push_back(0xaa);
                                                break;
                                            }
                                            case 0xad: {
                                                message.push_back(0xff);
                                                break;
                                            }
                                            case 0xae: {
                                                if (!specialMessage) {
                                                    specialMessage = true;
                                                    specialMessageId = 0;
                                                }
                                                break;
                                            }
                                            case 0xaf: {
                                                if (!specialMessage) {
                                                    specialMessage = true;
                                                    specialMessageId = 1;
                                                }
                                                break;
                                            }
                                            case 0xba: {
                                                if (!specialMessage) {
                                                    specialMessage = true;
                                                    specialMessageId = 2;
                                                }
                                                break;
                                            }
                                            default: {
                                                readingMessage = false;
                                            }
                                        }
                                    } else {
                                        message.push_back(byte);
                                    }
                                }
                            }
                        } else {
                            if (byte == 0x00) {
                                message.clear();
                                readingMessage = true;
                                escapedCharacter = false;
                                specialMessage = false;
                            }
                        }
                    } catch (const std::runtime_error& exception) {
                        continue;
                    }
                }
            }, handleException));

            // manage socket connections
            eventLoops.push_back(make_eventLoop([&](std::atomic_bool& running) {
                const auto socketName = std::string("/var/run/buggy/arbiter.sock");
                const auto fileDescriptor = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fileDescriptor < 0) {
                    throw std::logic_error(std::string("creating the socket '") + socketName + "' failed");
                }
                {
                    sockaddr_un address;
                    address.sun_family = AF_UNIX;
                    unlink(socketName.c_str());
                    strncpy(address.sun_path, socketName.c_str(), sizeof(address.sun_path) - 1);
                    if (bind(fileDescriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
                        throw std::logic_error(std::string("binding the socket '") + socketName + "' failed");
                    }
                }
                if (listen(fileDescriptor, SOMAXCONN) < 0) {
                    throw std::logic_error(std::string("listening with socket '") + socketName + "' failed");
                }
                fd_set fileDescriptorsSet;
                timespec timeout;
                timeout.tv_sec = 1;
                timeout.tv_nsec = 0;
                while (running.load(std::memory_order_relaxed)) {
                    FD_ZERO(&fileDescriptorsSet);
                    FD_SET(fileDescriptor, &fileDescriptorsSet);
                    if (pselect(fileDescriptor + 1, &fileDescriptorsSet, nullptr, nullptr, &timeout, nullptr) < 0) {
                        throw std::logic_error(std::string("select with socket '") + socketName + "' failed");
                    }
                    if (FD_ISSET(fileDescriptor, &fileDescriptorsSet)) {
                        const auto newSocket = accept(fileDescriptor, nullptr, nullptr);
                        if (newSocket < 0) {
                            throw std::logic_error(std::string("accept with socket '") + socketName + "' failed");
                        }
                        std::lock_guard<std::mutex> lockGuard(socketsLock);
                        sockets.push_back(newSocket);
                    }
                }
                unlink(socketName.c_str());
            }, handleException));


            // listen to on-board script events
            eventLoops.push_back(make_eventLoop([&](std::atomic_bool& running) {
                const auto fifoName = std::string("/var/run/buggy/arbiter.fifo");
                unlink(fifoName.c_str());
                if (mkfifo(fifoName.c_str(), 0666) < 0) {
                    throw std::logic_error(std::string("creating the fifo '") + fifoName + "' failed");
                }
                const auto fileDescriptor = open(fifoName.c_str(), O_RDWR | O_NONBLOCK);
                if (fileDescriptor < 0) {
                    throw std::logic_error(std::string("opening the fifo '") + fifoName + "' failed");
                }
                auto bytes = std::array<uint8_t, 3>{};
                fd_set fileDescriptorsSet;
                timespec timeout;
                timeout.tv_sec = 1;
                timeout.tv_nsec = 0;
                while (running.load(std::memory_order_relaxed)) {
                    FD_ZERO(&fileDescriptorsSet);
                    FD_SET(fileDescriptor, &fileDescriptorsSet);
                    if (pselect(fileDescriptor + 1, &fileDescriptorsSet, nullptr, nullptr, &timeout, nullptr) < 0) {
                        throw std::logic_error(std::string("select with fifo '") + fifoName + "' failed");
                    }
                    if (FD_ISSET(fileDescriptor, &fileDescriptorsSet)) {
                        const auto bytesRead = read(fileDescriptor, bytes.data(), bytes.size());
                        if (bytesRead < 0) {
                            throw std::logic_error(std::string("reading from the fifo '") + fifoName + "' failed");
                        }
                        if (bytesRead == bytes.size()) {
                            {
                                std::lock_guard<std::mutex> lockGuard(indicesAndValuesLock);

                                // @DEBUG
                                std::cout << "Message received from script: {";
                                for (auto byte : bytes) {
                                    std::cout << +byte << ", ";
                                }
                                std::cout << "}" << std::endl;
                                // }

                                if (control.load(std::memory_order_release) == Control::base) {
                                    indicesAndValues.emplace_back(bytes[0], (static_cast<uint16_t>(bytes[2]) << 8) | static_cast<uint16_t>(bytes[1]));
                                }
                            }
                            indicesAndValuesChanged.notify_one();
                        } else if (bytesRead > 0) {
                            throw std::logic_error(std::string("reading from the fifo '") + fifoName + "' yielded an unexpected number of bytes");
                        }
                    }
                }
                unlink(fifoName.c_str());
            }, handleException));

            exceptionChanged.wait(uniqueLock);
            {
                std::lock_guard<std::mutex> lockGuard(indicesAndValuesLock);
                indicesAndValues.clear();
                indicesAndValues.emplace_back(1, std::get<1>(motorsZeros));
            }
            indicesAndValuesChanged.notify_one();
        }
        if (exception) {
            std::rethrow_exception(exception);
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << std::endl;
    }

    return 0;
}
