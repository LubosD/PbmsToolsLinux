#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pace {

class Serial {
public:
    ~Serial();
    Serial() = default;
    Serial(const Serial&)            = delete;
    Serial& operator=(const Serial&) = delete;
    Serial(Serial&&)                 = default;
    Serial& operator=(Serial&&)      = default;

    bool open(const std::string& port, int baud, int timeout_ms);
    void close();
    void flush();
    bool write(const std::vector<uint8_t>& data, std::string& err);
    bool read_frame(std::vector<uint8_t>& frame, std::string& err);
private:
    int fd_ = -1;
};

} // namespace pace
