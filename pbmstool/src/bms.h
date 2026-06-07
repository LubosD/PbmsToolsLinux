#pragma once
#include "protocol.h"
#include "serial.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pace {

struct AnalogData {
    int cell_count = 0;
    std::vector<double> cell_mv;
    int temp_count = 0;
    std::vector<double> temp_raw;   // Kelvin * 10
    int16_t  current_10ma = 0;      // signed, 10 mA per LSB
    uint16_t total_volt_mv = 0;     // mV
    int      remain_cap_mah = 0;
    int      full_cap_mah = 0;
    int      design_cap_mah = 0;
    uint16_t cycle = 0;
};

struct AlarmData {
    int cell_count = 0;
    std::vector<uint8_t> cell_volt_alarm;
    int temp_count = 0;
    std::vector<uint8_t> temp_alarm;
    uint8_t charge_curr_state = 0;
    uint8_t total_volt_state = 0;
    uint8_t discharge_curr_state = 0;
    std::array<uint8_t, 9> status = {};
};

struct BalanceParams {
    double threshold_v = 0.0;
    int    delta_mv = 0;
};

// Cell voltage over/under protection (OVP / UVP)
struct CellVoltProtParams {
    bool   enable    = false;
    double protect_v = 0.0;   // V
    double alarm_v   = 0.0;   // V
    double recover_v = 0.0;   // V
    int    delay_ms  = 0;     // raw * 100
};

// Charge / discharge overcurrent protection
struct CurrProtParams {
    bool   enable    = false;
    double protect_a = 0.0;   // A
    double alarm_a   = 0.0;   // A
    double recover_a = 0.0;   // A
    int    delay_ms  = 0;     // raw * 100
};

// Short-circuit protection
struct ShortCircuitParams {
    bool  enable    = false;
    int   threshold = 0;
    int   time_us   = 0;
    int   delay_ms  = 0;     // raw * 100
};

// Cell charge over-temperature protection
struct CellTempProtParams {
    bool  enable    = false;
    int   trigger_c = 0;     // °C (absolute value of signed int16)
    int   recover_c = 0;
    int   delay_ms  = 0;     // raw * 100
};

// MOSFET over-temperature protection
struct MosfetOtpParams {
    int   trigger_c = 0;     // °C (absolute value of signed int16)
    int   delay_ms  = 0;     // raw * 25
};

// Cell under-temperature protection
struct UnderTempParams {
    int   delay_ms  = 0;     // raw * 25
};

// Pack parameters group B (CMD 0xA0 / 0xA8) — params 28-29
struct PackParamsBParams {
    double volt_v = 0.0;     // V (raw / 1000)
    int    value  = 0;
};

// Cell equalization parameters (CMD 0xAF / 0xAE) — params 30-31, 53
struct EqualizationParams {
    double start_v  = 0.0;  // V (raw / 1000)
    int    delta_mv = 0;
    int    cell_cnt = 0;
};

// Temperature protection group with up to 6 sensors + enable flag.
// For 3-sensor groups (MOS chg temp, params 44-46) only temp[0..2] are used.
// present=false means the device returned RTN=4 (group not supported).
struct TempProtGroup {
    bool enable   = false;
    bool present  = true;    // false when device returns RTN=4
    int  temp[6]  = {};      // °C each; encoding: (°C + 273) * 10
};

class BMS {
public:
    explicit BMS(Serial& serial);

    // Returns pack count (1-15) or -1 on error.
    int  discover(std::string& err);

    bool get_version(uint8_t addr, std::string& version, std::string& err);
    bool get_analog (uint8_t addr, AnalogData&    out, std::string& err);
    bool get_alarm  (uint8_t addr, AlarmData&     out, std::string& err);

    // Pack params A (CMD 0xB6/0xB5) — params 26-27
    bool get_balance(BalanceParams& out, std::string& err);
    bool set_balance(const BalanceParams& p, std::string& err);

    // Cell voltage over-protection (CMD 0xD1/0xD0)
    bool get_cell_ovp(uint8_t addr, CellVoltProtParams& out, std::string& err);
    bool set_cell_ovp(const CellVoltProtParams& p, std::string& err);

    // Pack over-voltage protection (CMD 0xD5/0xD4)
    bool get_pack_ovp(uint8_t addr, CellVoltProtParams& out, std::string& err);
    bool set_pack_ovp(const CellVoltProtParams& p, std::string& err);

    // Charge overcurrent protection (CMD 0xD3/0xD2)
    bool get_chg_ocp(uint8_t addr, CurrProtParams& out, std::string& err);
    bool set_chg_ocp(const CurrProtParams& p, std::string& err);

    // Discharge overcurrent protection (CMD 0xD7/0xD6)
    bool get_dchg_ocp(uint8_t addr, CurrProtParams& out, std::string& err);
    bool set_dchg_ocp(const CurrProtParams& p, std::string& err);

    // Short-circuit protection (CMD 0xD9/0xD8)
    bool get_scp(ShortCircuitParams& out, std::string& err);
    bool set_scp(const ShortCircuitParams& p, std::string& err);

    // Cell charge over-temperature protection (CMD 0xDB/0xDA)
    bool get_cell_chg_otp(CellTempProtParams& out, std::string& err);
    bool set_cell_chg_otp(const CellTempProtParams& p, std::string& err);

    // MOSFET over-temperature protection (CMD 0xE3/0xE2)
    bool get_mosfet_otp(MosfetOtpParams& out, std::string& err);
    bool set_mosfet_otp(const MosfetOtpParams& p, std::string& err);

    // Cell under-temperature protection (CMD 0xE5/0xE4)
    bool get_utp(UnderTempParams& out, std::string& err);
    bool set_utp(const UnderTempParams& p, std::string& err);

    // Pack params B (CMD 0xA0/0xA8) — params 28-29
    bool get_pack_b(PackParamsBParams& out, std::string& err);
    bool set_pack_b(const PackParamsBParams& p, std::string& err);

    // Cell equalization (CMD 0xAF/0xAE) — params 30-31, 53
    bool get_equalization(EqualizationParams& out, std::string& err);
    bool set_equalization(const EqualizationParams& p, std::string& err);

    // Charge temperature protection 6-sensor group (CMD 0xDD/0xDC) — params 32-37
    bool get_chg_temp_prot(TempProtGroup& out, std::string& err);
    bool set_chg_temp_prot(const TempProtGroup& p, std::string& err);

    // Discharge temperature protection 6-sensor group (CMD 0xDF/0xDE) — params 38-43
    bool get_dchg_temp_prot(TempProtGroup& out, std::string& err);
    bool set_dchg_temp_prot(const TempProtGroup& p, std::string& err);

    // MOS charge temperature 3-sensor group (CMD 0xE1/0xE0) — params 44-46
    // present=false in out when device returns RTN=4 (not supported)
    bool get_mos_chg_temp(TempProtGroup& out, std::string& err);
    bool set_mos_chg_temp(const TempProtGroup& p, std::string& err);

    // MOS discharge temperature 6-sensor group (CMD 0xE7/0xE6) — params 47-52
    bool get_mos_dchg_temp(TempProtGroup& out, std::string& err);
    bool set_mos_dchg_temp(const TempProtGroup& p, std::string& err);

private:
    static constexpr int kMaxAttempts = 3;
    bool send_recv(uint8_t adr, uint8_t cmd,
                   const std::vector<uint8_t>& data,
                   Response& resp, std::string& err);
    Serial& serial_;
};

} // namespace pace
