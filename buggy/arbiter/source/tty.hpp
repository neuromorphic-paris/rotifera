#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <string>
#include <vector>

class Tty {
    public:
        Tty(const std::string& filename, uint64_t baudrate, uint64_t timeout) :
            _filename(filename),
            _fileDescriptor(open(filename.c_str(), O_RDWR | O_NOCTTY))
        {
            if (_fileDescriptor < 0) {
                throw std::runtime_error(std::string("opening '") + _filename + "' failed");
            }
            termios options;
            if (tcgetattr(_fileDescriptor, &options) < 0) {
                throw std::logic_error("getting the terminal options failed");
            }
            cfmakeraw(&options);
            cfsetispeed(&options, baudrate);
            cfsetospeed(&options, baudrate);
            options.c_cc[VMIN] = 0;
            options.c_cc[VTIME] = timeout;
            tcsetattr(_fileDescriptor, TCSANOW, &options);
            if (tcsetattr(_fileDescriptor, TCSAFLUSH, &options) < 0) {
                throw std::logic_error("setting the terminal options failed");
            }
            tcflush(_fileDescriptor, TCIOFLUSH);
        }
        Tty(const Tty&) = delete;
        Tty(Tty&&) = default;
        Tty& operator=(const Tty&) = delete;
        Tty& operator=(Tty&&) = default;
        virtual ~Tty() {}

        /// write sends data to the tty.
        virtual void write(const std::vector<uint8_t>& bytes) {
            ::write(_fileDescriptor, bytes.data(), bytes.size());
            tcdrain(_fileDescriptor);
        }

        /// read loads a single byte from the tty.
        uint8_t read() {
            uint8_t byte;
            const auto bytesRead = ::read(_fileDescriptor, &byte, 1);
            if (bytesRead <= 0) {
                if (access(_filename.c_str(), F_OK) < 0) {
                    throw std::logic_error(std::string("'") + _filename + "' disconnected");
                }
                throw std::runtime_error("read timeout");
            }
            return byte;
        }

    protected:
        const std::string _filename;
        int32_t _fileDescriptor;
};
