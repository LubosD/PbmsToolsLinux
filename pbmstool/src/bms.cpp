#include "bms.h"

namespace pace {

static constexpr uint8_t CMD_VERSION   = 0xC1;
static constexpr uint8_t CMD_ANALOG    = 0x42;
static constexpr uint8_t CMD_ALARM     = 0x44;
static constexpr uint8_t CMD_BAL_READ  = 0xB6;
static constexpr uint8_t CMD_BAL_WRITE = 0xB5;

BMS::BMS(Serial& serial) : serial_(serial) {}

bool BMS::send_recv(uint8_t adr, uint8_t cmd,
                    const std::vector<uint8_t>& data,
                    Response& resp, std::string& err) {
    auto frame = encode_frame(adr, cmd, data);
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (attempt > 0) serial_.flush();
        if (!serial_.write(frame, err)) return false;
        std::vector<uint8_t> raw;
        if (!serial_.read_frame(raw, err)) {
            if (attempt + 1 < kMaxAttempts) continue;
            return false;
        }
        if (!decode_frame(raw, resp, err)) {
            if (attempt + 1 < kMaxAttempts) continue;
            return false;
        }
        return true;
    }
    return false;
}

int BMS::discover(std::string& err) {
    int count = 0;
    int consecutive_fail = 0;
    for (uint8_t addr = 1; addr <= 15; ++addr) {
        Response resp;
        std::string e;
        if (send_recv(addr, CMD_VERSION, {}, resp, e) && resp.rtn == 0) {
            ++count;
            consecutive_fail = 0;
        } else {
            if (++consecutive_fail >= 3) break;
        }
    }
    if (count == 0) { err = "no packs found"; return -1; }
    return count;
}

bool BMS::get_version(uint8_t addr, std::string& version, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_VERSION, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    if (resp.data.size() < 20) { err = "short version response"; return false; }
    version = std::string(reinterpret_cast<const char*>(resp.data.data()), 20);
    while (!version.empty() && (version.back() == '\0' || version.back() == ' '))
        version.pop_back();
    return true;
}

} // namespace pace
