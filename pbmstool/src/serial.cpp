#include "serial.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace pace {

Serial::~Serial() { close(); }

static speed_t to_baud(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

bool Serial::open(const std::string& port, int baud, int timeout_ms) {
    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;

    // Switch to blocking with VTIME timeout
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty {};
    if (tcgetattr(fd_, &tty) != 0) { ::close(fd_); fd_ = -1; return false; }

    cfsetispeed(&tty, to_baud(baud));
    cfsetospeed(&tty, to_baud(baud));
    cfmakeraw(&tty);

    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag  = IGNBRK;
    tty.c_lflag  = 0;
    tty.c_oflag  = 0;

    // VTIME in 0.1s units; VMIN=0 => pure timeout mode
    int vtime = (timeout_ms + 99) / 100;  // round up to 0.1s
    if (vtime < 1) vtime = 1;
    if (vtime > 255) vtime = 255;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = static_cast<uint8_t>(vtime);

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) { ::close(fd_); fd_ = -1; return false; }
    tcflush(fd_, TCIOFLUSH);
    return true;
}

void Serial::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool Serial::write(const std::vector<uint8_t>& data, std::string& err) {
    ssize_t n = ::write(fd_, data.data(), data.size());
    if (n < 0 || static_cast<size_t>(n) != data.size()) {
        err = std::strerror(errno);
        return false;
    }
    return true;
}

bool Serial::read_frame(std::vector<uint8_t>& frame, std::string& err) {
    frame.clear();
    uint8_t byte;
    // Wait for start byte 0x7E
    while (true) {
        ssize_t n = ::read(fd_, &byte, 1);
        if (n <= 0) { err = (n == 0) ? "timeout" : std::strerror(errno); return false; }
        if (byte == 0x7E) { frame.push_back(byte); break; }
    }
    // Read until 0x0D end byte
    for (int i = 0; i < 2048; ++i) {
        ssize_t n = ::read(fd_, &byte, 1);
        if (n <= 0) { err = (n == 0) ? "timeout" : std::strerror(errno); return false; }
        frame.push_back(byte);
        if (byte == 0x0D) return true;
    }
    err = "frame too long";
    return false;
}

} // namespace pace
