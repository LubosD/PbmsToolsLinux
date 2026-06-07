#include "bms.h"
#include <cmath>

namespace pace {

static constexpr uint8_t CMD_VERSION      = 0xC1;
static constexpr uint8_t CMD_ANALOG       = 0x42;
static constexpr uint8_t CMD_ALARM        = 0x44;
static constexpr uint8_t CMD_BAL_READ     = 0xB6;  // pack params A read  (params 26-27)
static constexpr uint8_t CMD_BAL_WRITE    = 0xB5;  // pack params A write
static constexpr uint8_t CMD_CELL_OVP_R   = 0xD1;
static constexpr uint8_t CMD_CELL_OVP_W   = 0xD0;
static constexpr uint8_t CMD_CHG_OCP_R    = 0xD3;
static constexpr uint8_t CMD_CHG_OCP_W    = 0xD2;
static constexpr uint8_t CMD_PACK_OVP_R   = 0xD5;
static constexpr uint8_t CMD_PACK_OVP_W   = 0xD4;
static constexpr uint8_t CMD_DCHG_OCP_R   = 0xD7;
static constexpr uint8_t CMD_DCHG_OCP_W   = 0xD6;
static constexpr uint8_t CMD_SCP_R        = 0xD9;
static constexpr uint8_t CMD_SCP_W        = 0xD8;
static constexpr uint8_t CMD_CHG_OTP_R    = 0xDB;
static constexpr uint8_t CMD_CHG_OTP_W    = 0xDA;
static constexpr uint8_t CMD_CHG_TEMP_R   = 0xDD;
static constexpr uint8_t CMD_CHG_TEMP_W   = 0xDC;
static constexpr uint8_t CMD_DCHG_TEMP_R  = 0xDF;
static constexpr uint8_t CMD_DCHG_TEMP_W  = 0xDE;
static constexpr uint8_t CMD_MOS_CHG_R    = 0xE1;
static constexpr uint8_t CMD_MOS_CHG_W    = 0xE0;
static constexpr uint8_t CMD_MOSFET_OTP_R = 0xE3;
static constexpr uint8_t CMD_MOSFET_OTP_W = 0xE2;
static constexpr uint8_t CMD_UTP_R        = 0xE5;
static constexpr uint8_t CMD_UTP_W        = 0xE4;
static constexpr uint8_t CMD_MOS_DCHG_R   = 0xE7;
static constexpr uint8_t CMD_MOS_DCHG_W   = 0xE6;
static constexpr uint8_t CMD_PACK_B_R     = 0xA0;
static constexpr uint8_t CMD_PACK_B_W     = 0xA8;
static constexpr uint8_t CMD_EQUAL_R      = 0xAF;
static constexpr uint8_t CMD_EQUAL_W      = 0xAE;

BMS::BMS(Serial& serial) : serial_(serial) {}

bool BMS::send_recv(uint8_t adr, uint8_t cmd,
                    const std::vector<uint8_t>& data,
                    Response& resp, std::string& err) {
    auto frame = encode_frame(adr, cmd, data);
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        serial_.flush();  // discard any stale data from previous failed commands
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
    ++i;  // skip custom number byte (between remain and full cap)
    uint16_t full_raw  = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    out.full_cap_mah   = static_cast<int>(full_raw) * 10;
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

// --- Pack params A (balance start voltage / delta) — CMD 0xB6/0xB5 ---

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

// --- Shared helpers ---

static bool parse_volt_prot(const std::vector<uint8_t>& d,
                             CellVoltProtParams& out, std::string& err) {
    if (d.size() < 8) { err = "response too short"; return false; }
    out.enable    = d[0] > 0;
    out.protect_v = static_cast<uint16_t>((d[1] << 8) | d[2]) / 1000.0;
    out.alarm_v   = static_cast<uint16_t>((d[3] << 8) | d[4]) / 1000.0;
    out.recover_v = static_cast<uint16_t>((d[5] << 8) | d[6]) / 1000.0;
    out.delay_ms  = d[7] * 100;
    return true;
}

static std::vector<uint8_t> encode_volt_prot(const CellVoltProtParams& p) {
    uint16_t prot = static_cast<uint16_t>(static_cast<int>(p.protect_v * 1000.0 + 0.5));
    uint16_t alrm = static_cast<uint16_t>(static_cast<int>(p.alarm_v   * 1000.0 + 0.5));
    uint16_t recv = static_cast<uint16_t>(static_cast<int>(p.recover_v * 1000.0 + 0.5));
    uint8_t  dly  = static_cast<uint8_t>(p.delay_ms / 100);
    return {
        static_cast<uint8_t>(p.enable ? 1 : 0),
        static_cast<uint8_t>(prot >> 8), static_cast<uint8_t>(prot & 0xFF),
        static_cast<uint8_t>(alrm >> 8), static_cast<uint8_t>(alrm & 0xFF),
        static_cast<uint8_t>(recv >> 8), static_cast<uint8_t>(recv & 0xFF),
        dly,
    };
}

static bool parse_curr_prot(const std::vector<uint8_t>& d,
                             CurrProtParams& out, std::string& err) {
    if (d.size() < 8) { err = "response too short"; return false; }
    out.enable    = d[0] > 0;
    out.protect_a = static_cast<uint16_t>((d[1] << 8) | d[2]) / 100.0;
    out.alarm_a   = static_cast<uint16_t>((d[3] << 8) | d[4]) / 100.0;
    out.recover_a = static_cast<uint16_t>((d[5] << 8) | d[6]) / 100.0;
    out.delay_ms  = d[7] * 100;
    return true;
}

static std::vector<uint8_t> encode_curr_prot(const CurrProtParams& p) {
    uint16_t prot = static_cast<uint16_t>(static_cast<int>(p.protect_a * 100.0 + 0.5));
    uint16_t alrm = static_cast<uint16_t>(static_cast<int>(p.alarm_a   * 100.0 + 0.5));
    uint16_t recv = static_cast<uint16_t>(static_cast<int>(p.recover_a * 100.0 + 0.5));
    uint8_t  dly  = static_cast<uint8_t>(p.delay_ms / 100);
    return {
        static_cast<uint8_t>(p.enable ? 1 : 0),
        static_cast<uint8_t>(prot >> 8), static_cast<uint8_t>(prot & 0xFF),
        static_cast<uint8_t>(alrm >> 8), static_cast<uint8_t>(alrm & 0xFF),
        static_cast<uint8_t>(recv >> 8), static_cast<uint8_t>(recv & 0xFF),
        dly,
    };
}

// Encode temperature in °C to raw big-endian uint16: (°C + 273) * 10
static uint16_t enc_temp(int celsius) {
    return static_cast<uint16_t>((celsius + 273) * 10);
}

// Decode raw uint16 to °C: raw/10 - 273
static int dec_temp(uint16_t raw) {
    return static_cast<int>(raw) / 10 - 273;
}

static bool parse_temp_group6(const std::vector<uint8_t>& d,
                               TempProtGroup& out, std::string& err) {
    if (d.size() < 13) { err = "response too short"; return false; }
    out.enable  = d[0] > 0;
    out.present = true;
    for (int k = 0; k < 6; ++k) {
        uint16_t raw = static_cast<uint16_t>((d[1 + k*2] << 8) | d[2 + k*2]);
        out.temp[k] = dec_temp(raw);
    }
    return true;
}

static std::vector<uint8_t> encode_temp_group6(const TempProtGroup& p, int n_sensors) {
    std::vector<uint8_t> v;
    v.push_back(p.enable ? 1 : 0);
    for (int k = 0; k < n_sensors; ++k) {
        uint16_t raw = enc_temp(p.temp[k]);
        v.push_back(static_cast<uint8_t>(raw >> 8));
        v.push_back(static_cast<uint8_t>(raw & 0xFF));
    }
    return v;
}

// --- Cell OVP ---

bool BMS::get_cell_ovp(uint8_t addr, CellVoltProtParams& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_CELL_OVP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return parse_volt_prot(resp.data, out, err);
}

bool BMS::set_cell_ovp(const CellVoltProtParams& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_CELL_OVP_W, encode_volt_prot(p), resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Pack OVP ---

bool BMS::get_pack_ovp(uint8_t addr, CellVoltProtParams& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_PACK_OVP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return parse_volt_prot(resp.data, out, err);
}

bool BMS::set_pack_ovp(const CellVoltProtParams& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_PACK_OVP_W, encode_volt_prot(p), resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Charge OCP ---

bool BMS::get_chg_ocp(uint8_t addr, CurrProtParams& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_CHG_OCP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return parse_curr_prot(resp.data, out, err);
}

bool BMS::set_chg_ocp(const CurrProtParams& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_CHG_OCP_W, encode_curr_prot(p), resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Discharge OCP ---

bool BMS::get_dchg_ocp(uint8_t addr, CurrProtParams& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_DCHG_OCP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return parse_curr_prot(resp.data, out, err);
}

bool BMS::set_dchg_ocp(const CurrProtParams& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_DCHG_OCP_W, encode_curr_prot(p), resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Short-circuit protection ---

bool BMS::get_scp(ShortCircuitParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_SCP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    const auto& d = resp.data;
    if (d.size() < 6) { err = "scp response too short"; return false; }
    out.enable    = d[0] > 0;
    out.threshold = static_cast<int>(static_cast<uint16_t>((d[1] << 8) | d[2]));
    out.time_us   = static_cast<int>(static_cast<uint16_t>((d[3] << 8) | d[4]));
    out.delay_ms  = d[5] * 100;
    return true;
}

bool BMS::set_scp(const ShortCircuitParams& p, std::string& err) {
    uint16_t thr = static_cast<uint16_t>(p.threshold);
    uint16_t tim = static_cast<uint16_t>(p.time_us);
    std::vector<uint8_t> info = {
        static_cast<uint8_t>(p.enable ? 1 : 0),
        static_cast<uint8_t>(thr >> 8), static_cast<uint8_t>(thr & 0xFF),
        static_cast<uint8_t>(tim >> 8), static_cast<uint8_t>(tim & 0xFF),
        static_cast<uint8_t>(p.delay_ms / 100),
    };
    Response resp;
    if (!send_recv(0x00, CMD_SCP_W, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Cell charge over-temperature protection ---

bool BMS::get_cell_chg_otp(CellTempProtParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_CHG_OTP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    const auto& d = resp.data;
    if (d.size() < 6) { err = "chg_otp response too short"; return false; }
    out.enable    = d[0] > 0;
    out.trigger_c = std::abs(static_cast<int>(static_cast<int16_t>((d[1] << 8) | d[2])));
    out.recover_c = std::abs(static_cast<int>(static_cast<int16_t>((d[3] << 8) | d[4])));
    out.delay_ms  = d[5] * 100;
    return true;
}

bool BMS::set_cell_chg_otp(const CellTempProtParams& p, std::string& err) {
    int16_t trig = static_cast<int16_t>(p.trigger_c);
    int16_t recv = static_cast<int16_t>(p.recover_c);
    std::vector<uint8_t> info = {
        static_cast<uint8_t>(p.enable ? 1 : 0),
        static_cast<uint8_t>(static_cast<uint16_t>(trig) >> 8),
        static_cast<uint8_t>(static_cast<uint16_t>(trig) & 0xFF),
        static_cast<uint8_t>(static_cast<uint16_t>(recv) >> 8),
        static_cast<uint8_t>(static_cast<uint16_t>(recv) & 0xFF),
        static_cast<uint8_t>(p.delay_ms / 100),
    };
    Response resp;
    if (!send_recv(0x00, CMD_CHG_OTP_W, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- MOSFET over-temperature protection ---

bool BMS::get_mosfet_otp(MosfetOtpParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_MOSFET_OTP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    const auto& d = resp.data;
    if (d.size() < 3) { err = "mosfet_otp response too short"; return false; }
    out.trigger_c = std::abs(static_cast<int>(static_cast<int16_t>((d[0] << 8) | d[1])));
    out.delay_ms  = d[2] * 25;
    return true;
}

bool BMS::set_mosfet_otp(const MosfetOtpParams& p, std::string& err) {
    int16_t trig = static_cast<int16_t>(p.trigger_c);
    std::vector<uint8_t> info = {
        static_cast<uint8_t>(static_cast<uint16_t>(trig) >> 8),
        static_cast<uint8_t>(static_cast<uint16_t>(trig) & 0xFF),
        static_cast<uint8_t>(p.delay_ms / 25),
    };
    Response resp;
    if (!send_recv(0x00, CMD_MOSFET_OTP_W, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Cell under-temperature protection ---

bool BMS::get_utp(UnderTempParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_UTP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    const auto& d = resp.data;
    if (d.size() < 1) { err = "utp response too short"; return false; }
    out.delay_ms = d[0] * 25;
    return true;
}

bool BMS::set_utp(const UnderTempParams& p, std::string& err) {
    std::vector<uint8_t> info = { static_cast<uint8_t>(p.delay_ms / 25) };
    Response resp;
    if (!send_recv(0x00, CMD_UTP_W, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Pack params B (CMD 0xA0/0xA8) ---

bool BMS::get_pack_b(PackParamsBParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_PACK_B_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    const auto& d = resp.data;
    if (d.size() < 4) { err = "pack_b response too short"; return false; }
    out.volt_v = static_cast<uint16_t>((d[0] << 8) | d[1]) / 1000.0;
    out.value  = static_cast<int>(static_cast<uint16_t>((d[2] << 8) | d[3]));
    return true;
}

bool BMS::set_pack_b(const PackParamsBParams& p, std::string& err) {
    uint16_t raw_v = static_cast<uint16_t>(static_cast<int>(p.volt_v * 1000.0 + 0.5));
    uint16_t raw_i = static_cast<uint16_t>(p.value);
    std::vector<uint8_t> info = {
        static_cast<uint8_t>(raw_v >> 8), static_cast<uint8_t>(raw_v & 0xFF),
        static_cast<uint8_t>(raw_i >> 8), static_cast<uint8_t>(raw_i & 0xFF),
    };
    Response resp;
    if (!send_recv(0x00, CMD_PACK_B_W, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Cell equalization (CMD 0xAF/0xAE) ---

bool BMS::get_equalization(EqualizationParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_EQUAL_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    const auto& d = resp.data;
    if (d.size() < 4) { err = "equalization response too short"; return false; }
    out.start_v  = static_cast<uint16_t>((d[0] << 8) | d[1]) / 1000.0;
    out.delta_pack_mv = static_cast<int>(static_cast<uint16_t>((d[2] << 8) | d[3]));
    out.cell_cnt = (d.size() >= 5) ? static_cast<int>(d[4]) : 0;
    return true;
}

bool BMS::set_equalization(const EqualizationParams& p, std::string& err) {
    uint16_t raw_v = static_cast<uint16_t>(static_cast<int>(p.start_v * 1000.0 + 0.5));
    uint16_t raw_d = static_cast<uint16_t>(p.delta_pack_mv);
    std::vector<uint8_t> info = {
        static_cast<uint8_t>(raw_v >> 8), static_cast<uint8_t>(raw_v & 0xFF),
        static_cast<uint8_t>(raw_d >> 8), static_cast<uint8_t>(raw_d & 0xFF),
        static_cast<uint8_t>(p.cell_cnt),
    };
    Response resp;
    if (!send_recv(0x00, CMD_EQUAL_W, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Charge temperature protection 6-sensor group ---

bool BMS::get_chg_temp_prot(TempProtGroup& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_CHG_TEMP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return parse_temp_group6(resp.data, out, err);
}

bool BMS::set_chg_temp_prot(const TempProtGroup& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_CHG_TEMP_W, encode_temp_group6(p, 6), resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- Discharge temperature protection 6-sensor group ---

bool BMS::get_dchg_temp_prot(TempProtGroup& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_DCHG_TEMP_R, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return parse_temp_group6(resp.data, out, err);
}

bool BMS::set_dchg_temp_prot(const TempProtGroup& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_DCHG_TEMP_W, encode_temp_group6(p, 6), resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}

// --- MOS charge temperature 3-sensor group ---

bool BMS::get_mos_chg_temp(TempProtGroup& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_MOS_CHG_R, {}, resp, err)) return false;
    if (resp.rtn == 4) { out = TempProtGroup{}; out.present = false; return true; }
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    const auto& d = resp.data;
    if (d.size() < 7) { err = "mos_chg_temp response too short"; return false; }
    out.enable  = d[0] > 0;
    out.present = true;
    for (int k = 0; k < 3; ++k) {
        uint16_t raw = static_cast<uint16_t>((d[1 + k*2] << 8) | d[2 + k*2]);
        out.temp[k] = dec_temp(raw);
    }
    return true;
}

bool BMS::set_mos_chg_temp(const TempProtGroup& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_MOS_CHG_W, encode_temp_group6(p, 3), resp, err)) return false;
    if (resp.rtn != 0 && resp.rtn != 4) {
        err = "BMS error RTN=" + std::to_string(resp.rtn);
        return false;
    }
    return true;
}

// --- MOS discharge temperature 6-sensor group ---

bool BMS::get_mos_dchg_temp(TempProtGroup& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_MOS_DCHG_R, {}, resp, err)) return false;
    if (resp.rtn == 4) { out = TempProtGroup{}; out.present = false; return true; }
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return parse_temp_group6(resp.data, out, err);
}

bool BMS::set_mos_dchg_temp(const TempProtGroup& p, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_MOS_DCHG_W, encode_temp_group6(p, 6), resp, err)) return false;
    if (resp.rtn != 0 && resp.rtn != 4) {
        err = "BMS error RTN=" + std::to_string(resp.rtn);
        return false;
    }
    return true;
}

} // namespace pace
