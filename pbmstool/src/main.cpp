#include "bms.h"
#include "serial.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace pace;

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " --port <dev> [--baud <N>] [--timeout <ms>] [--pack <N>]"
                 " <subcommand>\n"
                 "Subcommands:\n"
                 "  discover\n"
                 "  info\n"
                 "  analog\n"
                 "  alarm\n"
                 "  params get\n"
                 "  params set [key=value ...]\n"
                 "\n"
                 "Settable parameter keys (params set):\n"
                 "  cell-ovp-enable, cell-ovp-protect, cell-ovp-alarm, cell-ovp-recover, cell-ovp-delay\n"
                 "  cell-uvp-enable, cell-uvp-protect, cell-uvp-alarm, cell-uvp-recover, cell-uvp-delay\n"
                 "  chg-ocp-enable, chg-ocp-protect, chg-ocp-alarm, chg-ocp-recover, chg-ocp-delay\n"
                 "  dchg-ocp-enable, dchg-ocp-protect, dchg-ocp-alarm, dchg-ocp-recover, dchg-ocp-delay\n"
                 "  scp-enable, scp-threshold, scp-time, scp-delay\n"
                 "  chg-otp-enable, chg-otp-trigger, chg-otp-recover, chg-otp-delay\n"
                 "  mosfet-otp-trigger, mosfet-otp-delay\n"
                 "  utp-delay\n"
                 "  balance-threshold, balance-delta\n"
                 "  pack-b-volt, pack-b-value\n"
                 "  equal-start, equal-delta, equal-cells\n"
                 "  chg-temp-enable, chg-temp-1 .. chg-temp-6\n"
                 "  dchg-temp-enable, dchg-temp-1 .. dchg-temp-6\n"
                 "  mos-chg-temp-enable, mos-chg-temp-1 .. mos-chg-temp-3\n"
                 "  mos-dchg-temp-enable, mos-dchg-temp-1 .. mos-dchg-temp-6\n";
}

static std::string get_kv(const std::string& arg, const std::string& key) {
    std::string prefix = key + "=";
    if (arg.substr(0, prefix.size()) == prefix) return arg.substr(prefix.size());
    return {};
}

// Helper: print a boolean param as 1/0
static std::string b2s(bool v) { return v ? "1" : "0"; }

int main(int argc, char* argv[]) {
    std::string port;
    int baud       = 9600;
    int timeout_ms = 2000;
    uint8_t pack   = 1;
    std::vector<std::string> subcmd;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" || a == "--baud" || a == "--timeout" || a == "--pack") {
            if (i+1 >= argc) { std::cerr << a << " requires a value\n"; return 1; }
            if      (a == "--port")    port       = argv[++i];
            else if (a == "--baud")    baud       = std::atoi(argv[++i]);
            else if (a == "--timeout") timeout_ms = std::atoi(argv[++i]);
            else { // --pack
                int v = std::atoi(argv[++i]);
                if (v < 1 || v > 15) { std::cerr << "--pack must be 1-15\n"; return 1; }
                pack = static_cast<uint8_t>(v);
            }
        } else if (a.substr(0,2) != "--") {
            subcmd.push_back(a);
        } else {
            std::cerr << "unknown flag: " << a << "\n"; return 1;
        }
    }

    if (port.empty() || subcmd.empty()) { usage(argv[0]); return 1; }

    Serial serial;
    std::string err;
    if (!serial.open(port, baud, timeout_ms)) {
        std::cerr << "cannot open " << port << ": " << std::strerror(errno) << "\n";
        return 1;
    }

    BMS bms(serial);

    // --- discover ---
    if (subcmd[0] == "discover") {
        int count = bms.discover(err);
        if (count < 0) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack_count=" << count << "\n";
        return 0;
    }

    // --- info ---
    if (subcmd[0] == "info") {
        std::string version;
        if (!bms.get_version(pack, version, err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack=" << static_cast<int>(pack) << "\nversion=" << version << "\n";
        return 0;
    }

    // --- analog ---
    if (subcmd[0] == "analog") {
        AnalogData a;
        if (!bms.get_analog(pack, a, err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack=" << static_cast<int>(pack) << "\n";
        std::cout << "cell_count=" << a.cell_count << "\n";
        for (int k = 0; k < a.cell_count; ++k)
            std::cout << "cell_" << (k+1) << "_mv=" << static_cast<int>(a.cell_mv[k]) << "\n";
        std::cout << "temp_count=" << a.temp_count << "\n";
        for (int k = 0; k < a.temp_count; ++k) {
            double tc = a.temp_raw[k] / 10.0 - 273.15;
            std::cout << "temp_" << (k+1) << "_c=" << std::fixed << std::setprecision(1) << tc << "\n";
        }
        std::cout << "current_a=" << std::fixed << std::setprecision(2)
                  << (a.current_10ma / 100.0) << "\n";
        std::cout << "total_voltage_mv=" << a.total_volt_mv << "\n";
        std::cout << "remain_cap_mah="   << a.remain_cap_mah << "\n";
        std::cout << "full_cap_mah="     << a.full_cap_mah   << "\n";
        std::cout << "design_cap_mah="   << a.design_cap_mah << "\n";
        std::cout << "cycle="            << a.cycle          << "\n";
        return 0;
    }

    // --- alarm ---
    if (subcmd[0] == "alarm") {
        AlarmData al;
        if (!bms.get_alarm(pack, al, err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack=" << static_cast<int>(pack) << "\n";
        std::cout << "cell_count=" << al.cell_count << "\n";
        for (int k = 0; k < al.cell_count; ++k)
            std::cout << "cell_" << (k+1) << "_volt_alarm=" << static_cast<int>(al.cell_volt_alarm[k]) << "\n";
        std::cout << "temp_count=" << al.temp_count << "\n";
        for (int k = 0; k < al.temp_count; ++k)
            std::cout << "temp_" << (k+1) << "_alarm=" << static_cast<int>(al.temp_alarm[k]) << "\n";
        std::cout << "charge_curr_state="    << static_cast<int>(al.charge_curr_state)    << "\n";
        std::cout << "total_volt_state="     << static_cast<int>(al.total_volt_state)     << "\n";
        std::cout << "discharge_curr_state=" << static_cast<int>(al.discharge_curr_state) << "\n";
        struct BitDef { uint8_t mask; const char* name; };
        auto print_bits = [&](uint8_t val, std::initializer_list<BitDef> defs) {
            for (auto& d : defs)
                std::cout << d.name << "=" << ((val & d.mask) ? 1 : 0) << "\n";
        };

        // Status1: protection triggers
        print_bits(al.status[0], {
            {0x01, "protect_cell_overvolt"},
            {0x02, "protect_cell_undervolt"},
            {0x04, "protect_pack_overvolt"},
            {0x08, "protect_pack_undervolt"},
            {0x10, "protect_chg_overcurr"},
            {0x20, "protect_dchg_overcurr"},
            {0x40, "protect_short_circuit"},
            {0x80, "protect_chg_overvolt"},
        });
        // Status2: temperature protection triggers
        print_bits(al.status[1], {
            {0x01, "protect_chg_temp_high"},
            {0x02, "protect_chg_temp_low"},
            {0x04, "protect_dchg_temp_high"},
            {0x08, "protect_dchg_temp_low"},
            {0x10, "protect_env_temp_high"},
            {0x20, "protect_env_temp_low"},
            {0x40, "protect_mosfet_temp_high"},
            {0x80, "fully_charged"},
        });
        // Status3: system switch state
        print_bits(al.status[2], {
            {0x01, "current_limit_active"},
            {0x02, "charge_mos_on"},
            {0x04, "discharge_mos_on"},
            {0x10, "reverse_connect"},
            {0x20, "ac_input_present"},
            {0x80, "heater_active"},
        });
        // Status4: additional switch states
        print_bits(al.status[3], {
            {0x01, "buzzer_off"},
            {0x10, "current_limit_enabled"},
            {0x20, "light_alarm_active"},
        });
        // Status5: hardware faults
        print_bits(al.status[4], {
            {0x01, "fault_charge_mos"},
            {0x02, "fault_discharge_mos"},
            {0x04, "fault_temp_sensor"},
            {0x10, "fault_cell"},
            {0x20, "fault_sampling"},
            {0x40, "fault_eeprom"},
            {0x80, "fault_rtc"},
        });
        // Status6+7: per-cell active balancing (bit0=cell1..bit7=cell8, then cell9-16)
        for (int c = 0; c < al.cell_count; ++c) {
            int byte_idx = (c < 8) ? 5 : 6;
            int bit = c % 8;
            std::cout << "cell_" << (c + 1) << "_balancing="
                      << ((al.status[byte_idx] >> bit) & 1) << "\n";
        }
        // Status8: soft alarm flags (warnings, non-tripping)
        print_bits(al.status[7], {
            {0x01, "alarm_cell_overvolt"},
            {0x02, "alarm_cell_undervolt"},
            {0x04, "alarm_pack_overvolt"},
            {0x08, "alarm_pack_undervolt"},
            {0x10, "alarm_chg_overcurr"},
            {0x20, "alarm_dchg_overcurr"},
        });
        // Status9: temperature alarm flags
        print_bits(al.status[8], {
            {0x01, "alarm_chg_temp_high"},
            {0x02, "alarm_chg_temp_low"},
            {0x04, "alarm_dchg_temp_high"},
            {0x08, "alarm_dchg_temp_low"},
            {0x10, "alarm_env_temp_high"},
            {0x20, "alarm_env_temp_low"},
            {0x40, "alarm_mosfet_temp_high"},
            {0x80, "alarm_mosfet_temp_prot"},
        });
        return 0;
    }

    // --- params ---
    if (subcmd[0] == "params") {
        if (subcmd.size() < 2) { usage(argv[0]); return 1; }

        // ------------------------------------------------------------------ get
        if (subcmd[1] == "get") {
            // Cell OVP
            {
                CellVoltProtParams p;
                if (!bms.get_cell_ovp(pack, p, err)) { std::cerr << "cell_ovp: " << err << "\n"; return 1; }
                std::cout << "cell_ovp_enable="    << b2s(p.enable) << "\n"
                          << std::fixed << std::setprecision(3)
                          << "cell_ovp_protect_v=" << p.protect_v << "\n"
                          << "cell_ovp_alarm_v="   << p.alarm_v   << "\n"
                          << "cell_ovp_recover_v=" << p.recover_v << "\n"
                          << "cell_ovp_delay_ms="  << p.delay_ms  << "\n";
            }
            // Cell UVP
            {
                CellVoltProtParams p;
                if (!bms.get_cell_uvp(pack, p, err)) { std::cerr << "cell_uvp: " << err << "\n"; return 1; }
                std::cout << "cell_uvp_enable="    << b2s(p.enable) << "\n"
                          << std::fixed << std::setprecision(3)
                          << "cell_uvp_protect_v=" << p.protect_v << "\n"
                          << "cell_uvp_alarm_v="   << p.alarm_v   << "\n"
                          << "cell_uvp_recover_v=" << p.recover_v << "\n"
                          << "cell_uvp_delay_ms="  << p.delay_ms  << "\n";
            }
            // Charge OCP
            {
                CurrProtParams p;
                if (!bms.get_chg_ocp(pack, p, err)) { std::cerr << "chg_ocp: " << err << "\n"; return 1; }
                std::cout << "chg_ocp_enable="    << b2s(p.enable) << "\n"
                          << std::fixed << std::setprecision(3)
                          << "chg_ocp_protect_a=" << p.protect_a << "\n"
                          << "chg_ocp_alarm_a="   << p.alarm_a   << "\n"
                          << "chg_ocp_recover_a=" << p.recover_a << "\n"
                          << "chg_ocp_delay_ms="  << p.delay_ms  << "\n";
            }
            // Discharge OCP
            {
                CurrProtParams p;
                if (!bms.get_dchg_ocp(pack, p, err)) { std::cerr << "dchg_ocp: " << err << "\n"; return 1; }
                std::cout << "dchg_ocp_enable="    << b2s(p.enable) << "\n"
                          << std::fixed << std::setprecision(3)
                          << "dchg_ocp_protect_a=" << p.protect_a << "\n"
                          << "dchg_ocp_alarm_a="   << p.alarm_a   << "\n"
                          << "dchg_ocp_recover_a=" << p.recover_a << "\n"
                          << "dchg_ocp_delay_ms="  << p.delay_ms  << "\n";
            }
            // Short-circuit protection
            {
                ShortCircuitParams p;
                if (!bms.get_scp(p, err)) { std::cerr << "scp: " << err << "\n"; return 1; }
                std::cout << "scp_enable="    << b2s(p.enable)    << "\n"
                          << "scp_threshold=" << p.threshold       << "\n"
                          << "scp_time_us="   << p.time_us         << "\n"
                          << "scp_delay_ms="  << p.delay_ms        << "\n";
            }
            // Cell charge OTP
            {
                CellTempProtParams p;
                if (!bms.get_cell_chg_otp(p, err)) { std::cerr << "chg_otp: " << err << "\n"; return 1; }
                std::cout << "chg_otp_enable="    << b2s(p.enable)  << "\n"
                          << "chg_otp_trigger_c=" << p.trigger_c     << "\n"
                          << "chg_otp_recover_c=" << p.recover_c     << "\n"
                          << "chg_otp_delay_ms="  << p.delay_ms      << "\n";
            }
            // MOSFET OTP
            {
                MosfetOtpParams p;
                if (!bms.get_mosfet_otp(p, err)) { std::cerr << "mosfet_otp: " << err << "\n"; return 1; }
                std::cout << "mosfet_otp_trigger_c=" << p.trigger_c << "\n"
                          << "mosfet_otp_delay_ms="  << p.delay_ms  << "\n";
            }
            // Under-temperature protection
            {
                UnderTempParams p;
                if (!bms.get_utp(p, err)) { std::cerr << "utp: " << err << "\n"; return 1; }
                std::cout << "utp_delay_ms=" << p.delay_ms << "\n";
            }
            // Pack params A (balance threshold / delta)
            {
                BalanceParams p;
                if (!bms.get_balance(p, err)) { std::cerr << "balance: " << err << "\n"; return 1; }
                std::cout << std::fixed << std::setprecision(3)
                          << "balance_threshold_v=" << p.threshold_v << "\n"
                          << "balance_delta_mv="    << p.delta_mv    << "\n";
            }
            // Pack params B
            {
                PackParamsBParams p;
                if (!bms.get_pack_b(p, err)) { std::cerr << "pack_b: " << err << "\n"; return 1; }
                std::cout << std::fixed << std::setprecision(3)
                          << "pack_b_volt_v=" << p.volt_v << "\n"
                          << "pack_b_value="  << p.value  << "\n";
            }
            // Equalization
            {
                EqualizationParams p;
                if (!bms.get_equalization(p, err)) { std::cerr << "equalization: " << err << "\n"; return 1; }
                std::cout << std::fixed << std::setprecision(3)
                          << "equal_start_v="  << p.start_v  << "\n"
                          << "equal_delta_mv=" << p.delta_mv << "\n"
                          << "equal_cell_cnt=" << p.cell_cnt << "\n";
            }
            // Charge temperature protection
            {
                TempProtGroup p;
                if (!bms.get_chg_temp_prot(p, err)) { std::cerr << "chg_temp: " << err << "\n"; return 1; }
                std::cout << "chg_temp_enable=" << b2s(p.enable) << "\n";
                for (int k = 0; k < 6; ++k)
                    std::cout << "chg_temp_" << (k+1) << "_c=" << p.temp[k] << "\n";
            }
            // Discharge temperature protection
            {
                TempProtGroup p;
                if (!bms.get_dchg_temp_prot(p, err)) { std::cerr << "dchg_temp: " << err << "\n"; return 1; }
                std::cout << "dchg_temp_enable=" << b2s(p.enable) << "\n";
                for (int k = 0; k < 6; ++k)
                    std::cout << "dchg_temp_" << (k+1) << "_c=" << p.temp[k] << "\n";
            }
            // MOS charge temperature (optional — device may not support)
            {
                TempProtGroup p;
                if (!bms.get_mos_chg_temp(p, err)) { std::cerr << "mos_chg_temp: " << err << "\n"; return 1; }
                if (p.present) {
                    std::cout << "mos_chg_temp_enable=" << b2s(p.enable) << "\n";
                    for (int k = 0; k < 3; ++k)
                        std::cout << "mos_chg_temp_" << (k+1) << "_c=" << p.temp[k] << "\n";
                }
            }
            // MOS discharge temperature (optional)
            {
                TempProtGroup p;
                if (!bms.get_mos_dchg_temp(p, err)) { std::cerr << "mos_dchg_temp: " << err << "\n"; return 1; }
                if (p.present) {
                    std::cout << "mos_dchg_temp_enable=" << b2s(p.enable) << "\n";
                    for (int k = 0; k < 6; ++k)
                        std::cout << "mos_dchg_temp_" << (k+1) << "_c=" << p.temp[k] << "\n";
                }
            }
            return 0;
        }

        // ------------------------------------------------------------------ set
        if (subcmd[1] == "set") {
            if (subcmd.size() < 3) {
                std::cerr << "params set: specify at least one key=value\n";
                return 1;
            }

            // Flags tracking which groups have at least one key specified
            bool has_ovp = false, has_uvp = false;
            bool has_chg_ocp = false, has_dchg_ocp = false;
            bool has_scp = false, has_chg_otp = false;
            bool has_mosfet_otp = false, has_utp = false;
            bool has_balance = false, has_pack_b = false;
            bool has_equal = false;
            bool has_chg_temp = false, has_dchg_temp = false;
            bool has_mos_chg = false, has_mos_dchg = false;

            // Parsed values (filled from args, then merged with current if partial)
            CellVoltProtParams ovp, uvp;
            CurrProtParams chg_ocp, dchg_ocp;
            ShortCircuitParams scp;
            CellTempProtParams chg_otp;
            MosfetOtpParams mosfet_otp;
            UnderTempParams utp;
            BalanceParams balance;
            PackParamsBParams pack_b;
            EqualizationParams equal;
            TempProtGroup chg_temp, dchg_temp, mos_chg, mos_dchg;

            // Per-field flags for merge logic
            struct {
                bool ovp_en=false, ovp_prot=false, ovp_alrm=false, ovp_rec=false, ovp_dly=false;
                bool uvp_en=false, uvp_prot=false, uvp_alrm=false, uvp_rec=false, uvp_dly=false;
                bool chg_en=false, chg_prot=false, chg_alrm=false, chg_rec=false, chg_dly=false;
                bool dchg_en=false, dchg_prot=false, dchg_alrm=false, dchg_rec=false, dchg_dly=false;
                bool scp_en=false, scp_thr=false, scp_tim=false, scp_dly=false;
                bool otp_en=false, otp_trig=false, otp_rec=false, otp_dly=false;
                bool motp_trig=false, motp_dly=false;
                bool utp_dly=false;
                bool bal_thr=false, bal_dlt=false;
                bool pkb_v=false, pkb_val=false;
                bool eq_start=false, eq_dlt=false, eq_cnt=false;
                bool ct_en=false; bool ct[6]={};
                bool dt_en=false; bool dt[6]={};
                bool mc_en=false; bool mc[3]={};
                bool md_en=false; bool md[6]={};
            } seen;

            for (size_t i = 2; i < subcmd.size(); ++i) {
                const std::string& arg = subcmd[i];
                std::string v;

                // --- Cell OVP ---
                if (!(v=get_kv(arg,"cell-ovp-enable")).empty())  { ovp.enable    = std::atoi(v.c_str())!=0; seen.ovp_en=true;   has_ovp=true; continue; }
                if (!(v=get_kv(arg,"cell-ovp-protect")).empty()) { ovp.protect_v = std::atof(v.c_str());    seen.ovp_prot=true; has_ovp=true; continue; }
                if (!(v=get_kv(arg,"cell-ovp-alarm")).empty())   { ovp.alarm_v   = std::atof(v.c_str());    seen.ovp_alrm=true; has_ovp=true; continue; }
                if (!(v=get_kv(arg,"cell-ovp-recover")).empty()) { ovp.recover_v = std::atof(v.c_str());    seen.ovp_rec=true;  has_ovp=true; continue; }
                if (!(v=get_kv(arg,"cell-ovp-delay")).empty())   { ovp.delay_ms  = std::atoi(v.c_str());    seen.ovp_dly=true;  has_ovp=true; continue; }

                // --- Cell UVP ---
                if (!(v=get_kv(arg,"cell-uvp-enable")).empty())  { uvp.enable    = std::atoi(v.c_str())!=0; seen.uvp_en=true;   has_uvp=true; continue; }
                if (!(v=get_kv(arg,"cell-uvp-protect")).empty()) { uvp.protect_v = std::atof(v.c_str());    seen.uvp_prot=true; has_uvp=true; continue; }
                if (!(v=get_kv(arg,"cell-uvp-alarm")).empty())   { uvp.alarm_v   = std::atof(v.c_str());    seen.uvp_alrm=true; has_uvp=true; continue; }
                if (!(v=get_kv(arg,"cell-uvp-recover")).empty()) { uvp.recover_v = std::atof(v.c_str());    seen.uvp_rec=true;  has_uvp=true; continue; }
                if (!(v=get_kv(arg,"cell-uvp-delay")).empty())   { uvp.delay_ms  = std::atoi(v.c_str());    seen.uvp_dly=true;  has_uvp=true; continue; }

                // --- Charge OCP ---
                if (!(v=get_kv(arg,"chg-ocp-enable")).empty())   { chg_ocp.enable    = std::atoi(v.c_str())!=0; seen.chg_en=true;   has_chg_ocp=true; continue; }
                if (!(v=get_kv(arg,"chg-ocp-protect")).empty())  { chg_ocp.protect_a = std::atof(v.c_str());    seen.chg_prot=true; has_chg_ocp=true; continue; }
                if (!(v=get_kv(arg,"chg-ocp-alarm")).empty())    { chg_ocp.alarm_a   = std::atof(v.c_str());    seen.chg_alrm=true; has_chg_ocp=true; continue; }
                if (!(v=get_kv(arg,"chg-ocp-recover")).empty())  { chg_ocp.recover_a = std::atof(v.c_str());    seen.chg_rec=true;  has_chg_ocp=true; continue; }
                if (!(v=get_kv(arg,"chg-ocp-delay")).empty())    { chg_ocp.delay_ms  = std::atoi(v.c_str());    seen.chg_dly=true;  has_chg_ocp=true; continue; }

                // --- Discharge OCP ---
                if (!(v=get_kv(arg,"dchg-ocp-enable")).empty())  { dchg_ocp.enable    = std::atoi(v.c_str())!=0; seen.dchg_en=true;   has_dchg_ocp=true; continue; }
                if (!(v=get_kv(arg,"dchg-ocp-protect")).empty()) { dchg_ocp.protect_a = std::atof(v.c_str());    seen.dchg_prot=true; has_dchg_ocp=true; continue; }
                if (!(v=get_kv(arg,"dchg-ocp-alarm")).empty())   { dchg_ocp.alarm_a   = std::atof(v.c_str());    seen.dchg_alrm=true; has_dchg_ocp=true; continue; }
                if (!(v=get_kv(arg,"dchg-ocp-recover")).empty()) { dchg_ocp.recover_a = std::atof(v.c_str());    seen.dchg_rec=true;  has_dchg_ocp=true; continue; }
                if (!(v=get_kv(arg,"dchg-ocp-delay")).empty())   { dchg_ocp.delay_ms  = std::atoi(v.c_str());    seen.dchg_dly=true;  has_dchg_ocp=true; continue; }

                // --- SCP ---
                if (!(v=get_kv(arg,"scp-enable")).empty())    { scp.enable    = std::atoi(v.c_str())!=0; seen.scp_en=true;  has_scp=true; continue; }
                if (!(v=get_kv(arg,"scp-threshold")).empty()) { scp.threshold = std::atoi(v.c_str());    seen.scp_thr=true; has_scp=true; continue; }
                if (!(v=get_kv(arg,"scp-time")).empty())      { scp.time_us   = std::atoi(v.c_str());    seen.scp_tim=true; has_scp=true; continue; }
                if (!(v=get_kv(arg,"scp-delay")).empty())     { scp.delay_ms  = std::atoi(v.c_str());    seen.scp_dly=true; has_scp=true; continue; }

                // --- Cell charge OTP ---
                if (!(v=get_kv(arg,"chg-otp-enable")).empty())   { chg_otp.enable    = std::atoi(v.c_str())!=0; seen.otp_en=true;   has_chg_otp=true; continue; }
                if (!(v=get_kv(arg,"chg-otp-trigger")).empty())  { chg_otp.trigger_c = std::atoi(v.c_str());    seen.otp_trig=true; has_chg_otp=true; continue; }
                if (!(v=get_kv(arg,"chg-otp-recover")).empty())  { chg_otp.recover_c = std::atoi(v.c_str());    seen.otp_rec=true;  has_chg_otp=true; continue; }
                if (!(v=get_kv(arg,"chg-otp-delay")).empty())    { chg_otp.delay_ms  = std::atoi(v.c_str());    seen.otp_dly=true;  has_chg_otp=true; continue; }

                // --- MOSFET OTP ---
                if (!(v=get_kv(arg,"mosfet-otp-trigger")).empty()) { mosfet_otp.trigger_c = std::atoi(v.c_str()); seen.motp_trig=true; has_mosfet_otp=true; continue; }
                if (!(v=get_kv(arg,"mosfet-otp-delay")).empty())   { mosfet_otp.delay_ms  = std::atoi(v.c_str()); seen.motp_dly=true;  has_mosfet_otp=true; continue; }

                // --- UTP ---
                if (!(v=get_kv(arg,"utp-delay")).empty()) { utp.delay_ms = std::atoi(v.c_str()); seen.utp_dly=true; has_utp=true; continue; }

                // --- Balance (pack params A) ---
                if (!(v=get_kv(arg,"balance-threshold")).empty()) { balance.threshold_v = std::atof(v.c_str()); seen.bal_thr=true; has_balance=true; continue; }
                if (!(v=get_kv(arg,"balance-delta")).empty())     { balance.delta_mv    = std::atoi(v.c_str()); seen.bal_dlt=true; has_balance=true; continue; }

                // --- Pack params B ---
                if (!(v=get_kv(arg,"pack-b-volt")).empty())  { pack_b.volt_v = std::atof(v.c_str()); seen.pkb_v=true;   has_pack_b=true; continue; }
                if (!(v=get_kv(arg,"pack-b-value")).empty()) { pack_b.value  = std::atoi(v.c_str()); seen.pkb_val=true; has_pack_b=true; continue; }

                // --- Equalization ---
                if (!(v=get_kv(arg,"equal-start")).empty()) { equal.start_v  = std::atof(v.c_str()); seen.eq_start=true; has_equal=true; continue; }
                if (!(v=get_kv(arg,"equal-delta")).empty()) { equal.delta_mv = std::atoi(v.c_str()); seen.eq_dlt=true;   has_equal=true; continue; }
                if (!(v=get_kv(arg,"equal-cells")).empty()) { equal.cell_cnt = std::atoi(v.c_str()); seen.eq_cnt=true;   has_equal=true; continue; }

                // --- Charge temperature group ---
                if (!(v=get_kv(arg,"chg-temp-enable")).empty()) { chg_temp.enable = std::atoi(v.c_str())!=0; seen.ct_en=true; has_chg_temp=true; continue; }
                bool matched_ct = false;
                for (int k = 0; k < 6 && !matched_ct; ++k) {
                    std::string key = "chg-temp-" + std::to_string(k+1);
                    if (!(v=get_kv(arg,key)).empty()) { chg_temp.temp[k] = std::atoi(v.c_str()); seen.ct[k]=true; has_chg_temp=true; matched_ct=true; }
                }
                if (matched_ct) continue;

                // --- Discharge temperature group ---
                if (!(v=get_kv(arg,"dchg-temp-enable")).empty()) { dchg_temp.enable = std::atoi(v.c_str())!=0; seen.dt_en=true; has_dchg_temp=true; continue; }
                bool matched_dt = false;
                for (int k = 0; k < 6 && !matched_dt; ++k) {
                    std::string key = "dchg-temp-" + std::to_string(k+1);
                    if (!(v=get_kv(arg,key)).empty()) { dchg_temp.temp[k] = std::atoi(v.c_str()); seen.dt[k]=true; has_dchg_temp=true; matched_dt=true; }
                }
                if (matched_dt) continue;

                // --- MOS charge temperature group ---
                if (!(v=get_kv(arg,"mos-chg-temp-enable")).empty()) { mos_chg.enable = std::atoi(v.c_str())!=0; seen.mc_en=true; has_mos_chg=true; continue; }
                bool matched_mc = false;
                for (int k = 0; k < 3 && !matched_mc; ++k) {
                    std::string key = "mos-chg-temp-" + std::to_string(k+1);
                    if (!(v=get_kv(arg,key)).empty()) { mos_chg.temp[k] = std::atoi(v.c_str()); seen.mc[k]=true; has_mos_chg=true; matched_mc=true; }
                }
                if (matched_mc) continue;

                // --- MOS discharge temperature group ---
                if (!(v=get_kv(arg,"mos-dchg-temp-enable")).empty()) { mos_dchg.enable = std::atoi(v.c_str())!=0; seen.md_en=true; has_mos_dchg=true; continue; }
                bool matched_md = false;
                for (int k = 0; k < 6 && !matched_md; ++k) {
                    std::string key = "mos-dchg-temp-" + std::to_string(k+1);
                    if (!(v=get_kv(arg,key)).empty()) { mos_dchg.temp[k] = std::atoi(v.c_str()); seen.md[k]=true; has_mos_dchg=true; matched_md=true; }
                }
                if (matched_md) continue;

                std::cerr << "unknown param: " << arg << "\n"; return 1;
            }

            // Read-modify-write for each group that was partially specified

            if (has_ovp) {
                if (!seen.ovp_en || !seen.ovp_prot || !seen.ovp_alrm || !seen.ovp_rec || !seen.ovp_dly) {
                    CellVoltProtParams cur;
                    if (!bms.get_cell_ovp(pack, cur, err)) { std::cerr << "cell_ovp read: " << err << "\n"; return 1; }
                    if (!seen.ovp_en)   ovp.enable    = cur.enable;
                    if (!seen.ovp_prot) ovp.protect_v = cur.protect_v;
                    if (!seen.ovp_alrm) ovp.alarm_v   = cur.alarm_v;
                    if (!seen.ovp_rec)  ovp.recover_v = cur.recover_v;
                    if (!seen.ovp_dly)  ovp.delay_ms  = cur.delay_ms;
                }
                if (!bms.set_cell_ovp(ovp, err)) { std::cerr << "cell_ovp write: " << err << "\n"; return 1; }
            }

            if (has_uvp) {
                if (!seen.uvp_en || !seen.uvp_prot || !seen.uvp_alrm || !seen.uvp_rec || !seen.uvp_dly) {
                    CellVoltProtParams cur;
                    if (!bms.get_cell_uvp(pack, cur, err)) { std::cerr << "cell_uvp read: " << err << "\n"; return 1; }
                    if (!seen.uvp_en)   uvp.enable    = cur.enable;
                    if (!seen.uvp_prot) uvp.protect_v = cur.protect_v;
                    if (!seen.uvp_alrm) uvp.alarm_v   = cur.alarm_v;
                    if (!seen.uvp_rec)  uvp.recover_v = cur.recover_v;
                    if (!seen.uvp_dly)  uvp.delay_ms  = cur.delay_ms;
                }
                if (!bms.set_cell_uvp(uvp, err)) { std::cerr << "cell_uvp write: " << err << "\n"; return 1; }
            }

            if (has_chg_ocp) {
                if (!seen.chg_en || !seen.chg_prot || !seen.chg_alrm || !seen.chg_rec || !seen.chg_dly) {
                    CurrProtParams cur;
                    if (!bms.get_chg_ocp(pack, cur, err)) { std::cerr << "chg_ocp read: " << err << "\n"; return 1; }
                    if (!seen.chg_en)   chg_ocp.enable    = cur.enable;
                    if (!seen.chg_prot) chg_ocp.protect_a = cur.protect_a;
                    if (!seen.chg_alrm) chg_ocp.alarm_a   = cur.alarm_a;
                    if (!seen.chg_rec)  chg_ocp.recover_a = cur.recover_a;
                    if (!seen.chg_dly)  chg_ocp.delay_ms  = cur.delay_ms;
                }
                if (!bms.set_chg_ocp(chg_ocp, err)) { std::cerr << "chg_ocp write: " << err << "\n"; return 1; }
            }

            if (has_dchg_ocp) {
                if (!seen.dchg_en || !seen.dchg_prot || !seen.dchg_alrm || !seen.dchg_rec || !seen.dchg_dly) {
                    CurrProtParams cur;
                    if (!bms.get_dchg_ocp(pack, cur, err)) { std::cerr << "dchg_ocp read: " << err << "\n"; return 1; }
                    if (!seen.dchg_en)   dchg_ocp.enable    = cur.enable;
                    if (!seen.dchg_prot) dchg_ocp.protect_a = cur.protect_a;
                    if (!seen.dchg_alrm) dchg_ocp.alarm_a   = cur.alarm_a;
                    if (!seen.dchg_rec)  dchg_ocp.recover_a = cur.recover_a;
                    if (!seen.dchg_dly)  dchg_ocp.delay_ms  = cur.delay_ms;
                }
                if (!bms.set_dchg_ocp(dchg_ocp, err)) { std::cerr << "dchg_ocp write: " << err << "\n"; return 1; }
            }

            if (has_scp) {
                if (!seen.scp_en || !seen.scp_thr || !seen.scp_tim || !seen.scp_dly) {
                    ShortCircuitParams cur;
                    if (!bms.get_scp(cur, err)) { std::cerr << "scp read: " << err << "\n"; return 1; }
                    if (!seen.scp_en)  scp.enable    = cur.enable;
                    if (!seen.scp_thr) scp.threshold = cur.threshold;
                    if (!seen.scp_tim) scp.time_us   = cur.time_us;
                    if (!seen.scp_dly) scp.delay_ms  = cur.delay_ms;
                }
                if (!bms.set_scp(scp, err)) { std::cerr << "scp write: " << err << "\n"; return 1; }
            }

            if (has_chg_otp) {
                if (!seen.otp_en || !seen.otp_trig || !seen.otp_rec || !seen.otp_dly) {
                    CellTempProtParams cur;
                    if (!bms.get_cell_chg_otp(cur, err)) { std::cerr << "chg_otp read: " << err << "\n"; return 1; }
                    if (!seen.otp_en)   chg_otp.enable    = cur.enable;
                    if (!seen.otp_trig) chg_otp.trigger_c = cur.trigger_c;
                    if (!seen.otp_rec)  chg_otp.recover_c = cur.recover_c;
                    if (!seen.otp_dly)  chg_otp.delay_ms  = cur.delay_ms;
                }
                if (!bms.set_cell_chg_otp(chg_otp, err)) { std::cerr << "chg_otp write: " << err << "\n"; return 1; }
            }

            if (has_mosfet_otp) {
                if (!seen.motp_trig || !seen.motp_dly) {
                    MosfetOtpParams cur;
                    if (!bms.get_mosfet_otp(cur, err)) { std::cerr << "mosfet_otp read: " << err << "\n"; return 1; }
                    if (!seen.motp_trig) mosfet_otp.trigger_c = cur.trigger_c;
                    if (!seen.motp_dly)  mosfet_otp.delay_ms  = cur.delay_ms;
                }
                if (!bms.set_mosfet_otp(mosfet_otp, err)) { std::cerr << "mosfet_otp write: " << err << "\n"; return 1; }
            }

            if (has_utp) {
                if (!bms.set_utp(utp, err)) { std::cerr << "utp write: " << err << "\n"; return 1; }
            }

            if (has_balance) {
                if (!seen.bal_thr || !seen.bal_dlt) {
                    BalanceParams cur;
                    if (!bms.get_balance(cur, err)) { std::cerr << "balance read: " << err << "\n"; return 1; }
                    if (!seen.bal_thr) balance.threshold_v = cur.threshold_v;
                    if (!seen.bal_dlt) balance.delta_mv    = cur.delta_mv;
                }
                if (!bms.set_balance(balance, err)) { std::cerr << "balance write: " << err << "\n"; return 1; }
            }

            if (has_pack_b) {
                if (!seen.pkb_v || !seen.pkb_val) {
                    PackParamsBParams cur;
                    if (!bms.get_pack_b(cur, err)) { std::cerr << "pack_b read: " << err << "\n"; return 1; }
                    if (!seen.pkb_v)   pack_b.volt_v = cur.volt_v;
                    if (!seen.pkb_val) pack_b.value  = cur.value;
                }
                if (!bms.set_pack_b(pack_b, err)) { std::cerr << "pack_b write: " << err << "\n"; return 1; }
            }

            if (has_equal) {
                if (!seen.eq_start || !seen.eq_dlt || !seen.eq_cnt) {
                    EqualizationParams cur;
                    if (!bms.get_equalization(cur, err)) { std::cerr << "equalization read: " << err << "\n"; return 1; }
                    if (!seen.eq_start) equal.start_v  = cur.start_v;
                    if (!seen.eq_dlt)   equal.delta_mv = cur.delta_mv;
                    if (!seen.eq_cnt)   equal.cell_cnt = cur.cell_cnt;
                }
                if (!bms.set_equalization(equal, err)) { std::cerr << "equalization write: " << err << "\n"; return 1; }
            }

            if (has_chg_temp) {
                bool need_read = !seen.ct_en;
                for (int k = 0; k < 6 && !need_read; ++k) if (!seen.ct[k]) need_read = true;
                if (need_read) {
                    TempProtGroup cur;
                    if (!bms.get_chg_temp_prot(cur, err)) { std::cerr << "chg_temp read: " << err << "\n"; return 1; }
                    if (!seen.ct_en) chg_temp.enable = cur.enable;
                    for (int k = 0; k < 6; ++k) if (!seen.ct[k]) chg_temp.temp[k] = cur.temp[k];
                }
                if (!bms.set_chg_temp_prot(chg_temp, err)) { std::cerr << "chg_temp write: " << err << "\n"; return 1; }
            }

            if (has_dchg_temp) {
                bool need_read = !seen.dt_en;
                for (int k = 0; k < 6 && !need_read; ++k) if (!seen.dt[k]) need_read = true;
                if (need_read) {
                    TempProtGroup cur;
                    if (!bms.get_dchg_temp_prot(cur, err)) { std::cerr << "dchg_temp read: " << err << "\n"; return 1; }
                    if (!seen.dt_en) dchg_temp.enable = cur.enable;
                    for (int k = 0; k < 6; ++k) if (!seen.dt[k]) dchg_temp.temp[k] = cur.temp[k];
                }
                if (!bms.set_dchg_temp_prot(dchg_temp, err)) { std::cerr << "dchg_temp write: " << err << "\n"; return 1; }
            }

            if (has_mos_chg) {
                bool need_read = !seen.mc_en;
                for (int k = 0; k < 3 && !need_read; ++k) if (!seen.mc[k]) need_read = true;
                if (need_read) {
                    TempProtGroup cur;
                    if (!bms.get_mos_chg_temp(cur, err)) { std::cerr << "mos_chg_temp read: " << err << "\n"; return 1; }
                    if (!seen.mc_en) mos_chg.enable = cur.enable;
                    for (int k = 0; k < 3; ++k) if (!seen.mc[k]) mos_chg.temp[k] = cur.temp[k];
                }
                if (!bms.set_mos_chg_temp(mos_chg, err)) { std::cerr << "mos_chg_temp write: " << err << "\n"; return 1; }
            }

            if (has_mos_dchg) {
                bool need_read = !seen.md_en;
                for (int k = 0; k < 6 && !need_read; ++k) if (!seen.md[k]) need_read = true;
                if (need_read) {
                    TempProtGroup cur;
                    if (!bms.get_mos_dchg_temp(cur, err)) { std::cerr << "mos_dchg_temp read: " << err << "\n"; return 1; }
                    if (!seen.md_en) mos_dchg.enable = cur.enable;
                    for (int k = 0; k < 6; ++k) if (!seen.md[k]) mos_dchg.temp[k] = cur.temp[k];
                }
                if (!bms.set_mos_dchg_temp(mos_dchg, err)) { std::cerr << "mos_dchg_temp write: " << err << "\n"; return 1; }
            }

            std::cout << "ok\n";
            return 0;
        }

        usage(argv[0]); return 1;
    }

    usage(argv[0]);
    return 1;
}
