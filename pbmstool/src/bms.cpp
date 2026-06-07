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

bool BMS::get_analog(uint8_t addr, AnalogData& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_ANALOG, {addr}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }

    const auto& d = resp.data;
    if (d.size() < 3) { err = "analog response too short"; return false; }

    size_t i = 2;  // skip DATAFLAG and pack-address bytes
    out.cell_count = d[i++];
    if (out.cell_count > 16) { err = "cell count > 16"; return false; }
    if (d.size() < i + static_cast<size_t>(out.cell_count) * 2 + 1)
        { err = "analog response truncated (cells)"; return false; }

    out.cell_mv.resize(out.cell_count);
    for (int k = 0; k < out.cell_count; ++k) {
        out.cell_mv[k] = static_cast<double>((d[i] << 8) | d[i+1]);
        i += 2;
    }

    out.temp_count = d[i++];
    if (out.temp_count > 16) { err = "temp count > 16"; return false; }
    if (d.size() < i + static_cast<size_t>(out.temp_count) * 2 + 11)
        { err = "analog response truncated (temps)"; return false; }

    out.temp_raw.resize(out.temp_count);
    for (int k = 0; k < out.temp_count; ++k) {
        out.temp_raw[k] = static_cast<double>((d[i] << 8) | d[i+1]);
        i += 2;
    }

    out.current_10ma  = static_cast<int16_t>(static_cast<uint16_t>((d[i] << 8) | d[i+1])); i += 2;
    out.total_volt_mv = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    uint16_t rem_raw  = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    out.remain_cap_mah = static_cast<int>(rem_raw) * 10;
    uint16_t full_raw  = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    out.full_cap_mah   = static_cast<int>(full_raw) * 10;
    ++i;  // skip custom number byte
    out.cycle          = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    if (i + 1 < d.size()) {
        uint16_t des_raw   = static_cast<uint16_t>((d[i] << 8) | d[i+1]);
        out.design_cap_mah = static_cast<int>(des_raw) * 10;
    }

    return true;
}

bool BMS::get_alarm(uint8_t addr, AlarmData& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_ALARM, {addr}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }

    const auto& d = resp.data;
    if (d.size() < 3) { err = "alarm response too short"; return false; }

    size_t i = 2;
    out.cell_count = d[i++];
    if (out.cell_count > 16) { err = "cell count > 16"; return false; }
    if (d.size() < i + static_cast<size_t>(out.cell_count) + 1)
        { err = "alarm response truncated (cells)"; return false; }

    out.cell_volt_alarm.assign(d.begin() + i, d.begin() + i + out.cell_count);
    i += out.cell_count;

    out.temp_count = d[i++];
    if (out.temp_count > 16) { err = "temp count > 16"; return false; }
    // 12 = 3 state bytes (charge/total/discharge) + 9 status bytes
    if (d.size() < i + static_cast<size_t>(out.temp_count) + 12)
        { err = "alarm response truncated (temps)"; return false; }

    out.temp_alarm.assign(d.begin() + i, d.begin() + i + out.temp_count);
    i += out.temp_count;

    out.charge_curr_state    = d[i++];
    out.total_volt_state     = d[i++];
    out.discharge_curr_state = d[i++];

    for (int k = 0; k < 9; ++k)
        out.status[k] = d[i++];

    return true;
}

bool BMS::get_balance(BalanceParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_BAL_READ, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    if (resp.data.size() < 4) { err = "balance response too short"; return false; }
    const auto& d = resp.data;
    uint16_t raw_thresh = static_cast<uint16_t>((d[0] << 8) | d[1]);
    uint16_t raw_delta  = static_cast<uint16_t>((d[2] << 8) | d[3]);
    out.threshold_v = raw_thresh / 1000.0;
    out.delta_mv    = static_cast<int>(raw_delta);
    return true;
}

bool BMS::set_balance(const BalanceParams& p, std::string& err) {
    if (p.threshold_v <= 0.0 || p.delta_mv < 0) {
        err = "invalid balance params";
        return false;
    }
    uint16_t raw_thresh = static_cast<uint16_t>(static_cast<int>(p.threshold_v * 1000.0 + 0.5));
    uint16_t raw_delta  = static_cast<uint16_t>(p.delta_mv);
    std::vector<uint8_t> info = {
        static_cast<uint8_t>(raw_thresh >> 8),
        static_cast<uint8_t>(raw_thresh & 0xFF),
        static_cast<uint8_t>(raw_delta >> 8),
        static_cast<uint8_t>(raw_delta & 0xFF),
    };
    Response resp;
    if (!send_recv(0x00, CMD_BAL_WRITE, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

} // namespace pace
