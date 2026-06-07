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

class BMS {
public:
    explicit BMS(Serial& serial);

    // Returns pack count (1-15) or -1 on error.
    int  discover(std::string& err);

    bool get_version(uint8_t addr, std::string& version, std::string& err);
    bool get_analog (uint8_t addr, AnalogData&    out, std::string& err);
    bool get_alarm  (uint8_t addr, AlarmData&     out, std::string& err);
    bool get_balance(BalanceParams& out, std::string& err);
    bool set_balance(const BalanceParams& p, std::string& err);

private:
    static constexpr int kMaxAttempts = 3;
    bool send_recv(uint8_t adr, uint8_t cmd,
                   const std::vector<uint8_t>& data,
                   Response& resp, std::string& err);
    Serial& serial_;
};

} // namespace pace
