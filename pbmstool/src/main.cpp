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
                 "Subcommands: discover | info | analog | alarm |"
                 " params get | params set balance-threshold=X.XX [balance-delta=N]\n";
}

static std::string get_kv(const std::string& arg, const std::string& key) {
    std::string prefix = key + "=";
    if (arg.substr(0, prefix.size()) == prefix) return arg.substr(prefix.size());
    return {};
}

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
        for (int k = 0; k < 9; ++k)
            std::cout << "status" << (k+1) << "=0x"
                      << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(al.status[k]) << std::dec << "\n";
        return 0;
    }

    // --- params ---
    if (subcmd[0] == "params") {
        if (subcmd.size() < 2) { usage(argv[0]); return 1; }

        if (subcmd[1] == "get") {
            BalanceParams bp;
            if (!bms.get_balance(bp, err)) { std::cerr << err << "\n"; return 1; }
            std::cout << std::fixed << std::setprecision(3)
                      << "balance_threshold_v=" << bp.threshold_v << "\n"
                      << "balance_delta_mv="    << bp.delta_mv    << "\n";
            return 0;
        }

        if (subcmd[1] == "set") {
            BalanceParams bp;
            bool has_thresh = false, has_delta = false;
            for (size_t i = 2; i < subcmd.size(); ++i) {
                std::string v = get_kv(subcmd[i], "balance-threshold");
                if (!v.empty()) { bp.threshold_v = std::atof(v.c_str()); has_thresh = true; continue; }
                v = get_kv(subcmd[i], "balance-delta");
                if (!v.empty()) { bp.delta_mv = std::atoi(v.c_str()); has_delta = true; continue; }
                std::cerr << "unknown param: " << subcmd[i] << "\n"; return 1;
            }
            if (!has_thresh && !has_delta) {
                std::cerr << "params set: specify balance-threshold and/or balance-delta\n";
                return 1;
            }
            // Read current values for any param not specified
            if (!has_thresh || !has_delta) {
                BalanceParams cur;
                if (!bms.get_balance(cur, err)) { std::cerr << err << "\n"; return 1; }
                if (!has_thresh) bp.threshold_v = cur.threshold_v;
                if (!has_delta)  bp.delta_mv    = cur.delta_mv;
            }
            if (!bms.set_balance(bp, err)) { std::cerr << err << "\n"; return 1; }
            std::cout << "ok\n";
            return 0;
        }

        usage(argv[0]); return 1;
    }

    usage(argv[0]);
    return 1;
}
