#include "tui.h"
#include <ncurses.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace pace {

// Color pair IDs
static constexpr int CP_NORMAL     = 1;  // white on black
static constexpr int CP_BALANCE    = 2;  // cyan — balancing cell
static constexpr int CP_OVP_WARN   = 3;  // yellow — OVP alarm threshold
static constexpr int CP_OVP_FAULT  = 4;  // bright green — OVP protect tripped
static constexpr int CP_UVP_WARN   = 5;  // yellow+bold (≈ orange) — UVP alarm threshold
static constexpr int CP_UVP_FAULT  = 6;  // red+bold — UVP protect tripped
static constexpr int CP_OK         = 7;  // green — OK / ON status
static constexpr int CP_SELECTED   = 8;  // black on white — param cursor row
static constexpr int CP_TAB_ACTIVE = 9;  // black on cyan — active tab label
static constexpr int CP_HEADER     = 10; // white on blue — header bar

// Layout: rows 0=header 1=tabbar 2..LINES-2=content LINES-1=footer
static constexpr int ROW_HEADER  = 0;
static constexpr int ROW_TABBAR  = 1;
static constexpr int ROW_CONTENT = 2;

TUI::TUI(BMS& bms, const std::string& port)
    : bms_(bms), port_(port),
      last_refresh_(std::chrono::steady_clock::now()) {}

TUI::~TUI() {
    if (isendwin() == FALSE) endwin();
}

void TUI::init_ncurses() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_NORMAL,     COLOR_WHITE,   COLOR_BLACK);
        init_pair(CP_BALANCE,    COLOR_CYAN,    COLOR_BLACK);
        init_pair(CP_OVP_WARN,   COLOR_YELLOW,  COLOR_BLACK);
        init_pair(CP_OVP_FAULT,  COLOR_GREEN,   COLOR_BLACK);
        init_pair(CP_UVP_WARN,   COLOR_YELLOW,  COLOR_BLACK);
        init_pair(CP_UVP_FAULT,  COLOR_RED,     COLOR_BLACK);
        init_pair(CP_OK,         COLOR_GREEN,   COLOR_BLACK);
        init_pair(CP_SELECTED,   COLOR_BLACK,   COLOR_WHITE);
        init_pair(CP_TAB_ACTIVE, COLOR_BLACK,   COLOR_CYAN);
        init_pair(CP_HEADER,     COLOR_WHITE,   COLOR_BLUE);
    }
}

void TUI::run() {
    init_ncurses();

    // Discover packs
    {
        std::string err;
        int n = bms_.discover(err);
        pack_count_ = (n > 0) ? n : 1;
    }

    // Initial data load
    refresh_live_data();
    {
        std::string err;
        load_params(err);
    }

    // Position cursor on first editable row
    param_cursor_ = 0;
    while (param_cursor_ < static_cast<int>(param_rows_.size()) &&
           param_rows_[param_cursor_].kind == ParamRow::Kind::HEADING)
        ++param_cursor_;

    draw_all();

    // 2-second refresh timeout
    timeout(2000);

    while (true) {
        int ch = getch();

        if (ch == 'q' || ch == 'Q') break;

        if (ch == KEY_F(1)) {
            current_tab_ = 0;
            // Reset param cursor to first editable row
            param_cursor_ = 0;
            while (param_cursor_ < static_cast<int>(param_rows_.size()) &&
                   param_rows_[param_cursor_].kind == ParamRow::Kind::HEADING)
                ++param_cursor_;
            edit_mode_ = false;
        }
        else if (ch == KEY_F(2)) { current_tab_ = 1; edit_mode_ = false; }
        else if (ch == KEY_F(3)) { current_tab_ = 2; edit_mode_ = false; }
        else if ((ch == '[' || ch == '<') && !edit_mode_) switch_pack(-1);
        else if ((ch == ']' || ch == '>') && !edit_mode_) switch_pack(+1);
        else if (ch == 'r' || ch == 'R') {
            if (current_tab_ != 2) auto_refresh_ = !auto_refresh_;
        }
        else if (ch == ERR) {
            // timeout fired — auto refresh
            if (auto_refresh_ && current_tab_ != 2) refresh_live_data();
        }
        else {
            if (current_tab_ == 2) handle_params_key(ch);
        }

        draw_all();
    }

    endwin();
}

void TUI::draw_header() {
    int cols = COLS;
    attron(COLOR_PAIR(CP_HEADER));
    mvhline(ROW_HEADER, 0, ' ', cols);

    // Left: "pbmstool  <port>"
    mvprintw(ROW_HEADER, 1, "pbmstool  %s", port_.c_str());

    // Center: pack switcher
    std::string pack_str;
    if (pack_count_ > 1) {
        pack_str = "Pack [< " + std::to_string(current_pack_) + " >] /"
                 + std::to_string(pack_count_);
    } else {
        pack_str = "Pack " + std::to_string(current_pack_);
    }
    int cx = (cols - static_cast<int>(pack_str.size())) / 2;
    if (cx > 0) mvprintw(ROW_HEADER, cx, "%s", pack_str.c_str());

    // Right: refresh indicator + quit hint
    const char* ref_lbl = auto_refresh_ ? "ON " : "OFF";
    std::string right = std::string("refresh:") + ref_lbl + "  [r] [q:quit]";
    int rx = cols - static_cast<int>(right.size()) - 1;
    if (rx > 0) mvprintw(ROW_HEADER, rx, "%s", right.c_str());

    attroff(COLOR_PAIR(CP_HEADER));

    // Re-color the ON/OFF indicator
    if (has_colors() && rx > 0) {
        int on_col = rx + 8; // after "refresh:"
        if (auto_refresh_) {
            mvchgat(ROW_HEADER, on_col, 3, A_BOLD, CP_OK, nullptr);
        } else {
            mvchgat(ROW_HEADER, on_col, 3, A_DIM,  CP_HEADER, nullptr);
        }
    }
}

void TUI::draw_tabs() {
    static const char* labels[] = { " F1:Analog ", " F2:Alarms ", " F3:Params " };
    move(ROW_TABBAR, 0);
    clrtoeol();

    int col = 0;
    for (int t = 0; t < 3; ++t) {
        if (t == current_tab_) {
            attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
            mvprintw(ROW_TABBAR, col, "%s", labels[t]);
            attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        } else {
            attron(COLOR_PAIR(CP_NORMAL));
            mvprintw(ROW_TABBAR, col, "%s", labels[t]);
            attroff(COLOR_PAIR(CP_NORMAL));
        }
        col += static_cast<int>(strlen(labels[t]));
        mvaddch(ROW_TABBAR, col, ACS_VLINE);
        col += 1;
    }
    mvhline(ROW_TABBAR, col, ACS_HLINE, COLS - col);
}

void TUI::draw_footer() {
    int row = LINES - 1;
    move(row, 0);
    clrtoeol();

    if (!footer_msg_.empty()) {
        attron(A_BOLD);
        mvprintw(row, 0, "%s", footer_msg_.c_str());
        attroff(A_BOLD);
        return;
    }

    if (current_tab_ != 2) {
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - last_refresh_).count();
        char buf[64];
        snprintf(buf, sizeof(buf), "Last update: %.1fs ago", secs);
        mvprintw(row, 0, "%s", buf);
    } else {
        mvprintw(row, 0, "%-*s",
            COLS - 1,
            "Up/Dn: navigate  digits: edit  Space: toggle  s: save  Esc: revert");
    }
}

void TUI::draw_content() {
    for (int r = ROW_CONTENT; r < LINES - 1; ++r) {
        move(r, 0);
        clrtoeol();
    }
    switch (current_tab_) {
        case 0: draw_analog(); break;
        case 1: draw_alarms(); break;
        case 2: draw_params(); break;
    }
}

void TUI::draw_all() {
    erase();
    draw_header();
    draw_tabs();
    draw_content();
    draw_footer();
    refresh();
}

int TUI::cell_color_pair(uint8_t volt_alarm, bool balancing, attr_t& extra_attr) {
    extra_attr = A_NORMAL;
    // Priority: UVP fault > OVP fault > UVP warn > OVP warn > balancing > normal
    if (volt_alarm & 0x02) { extra_attr = A_BOLD; return CP_UVP_FAULT; }
    if (volt_alarm & 0x08) { extra_attr = A_BOLD; return CP_OVP_FAULT; }
    if (volt_alarm & 0x01) { extra_attr = A_BOLD; return CP_UVP_WARN; }
    if (volt_alarm & 0x04) { return CP_OVP_WARN; }
    if (balancing)         { return CP_BALANCE; }
    return CP_NORMAL;
}

void TUI::draw_analog() {
    int row = ROW_CONTENT;
    int cols = COLS;

    int left_w = cols / 2;
    int bar_max = left_w - 18;
    if (bar_max < 4) bar_max = 4;

    // Voltage range for bar scaling (in mV)
    double v_min = 2500.0, v_max = 4200.0;
    if (!analog_.cell_mv.empty()) {
        v_min = *std::min_element(analog_.cell_mv.begin(), analog_.cell_mv.end()) - 50.0;
        v_max = *std::max_element(analog_.cell_mv.begin(), analog_.cell_mv.end()) + 50.0;
        if (v_max - v_min < 100.0) { v_min -= 50.0; v_max += 50.0; }
    }

    mvprintw(row++, 0, "  CELL VOLTAGES");

    for (int k = 0; k < analog_.cell_count && row < LINES - 2; ++k) {
        double mv = analog_.cell_mv[k];
        double v  = mv / 1000.0;

        // Balancing from status bytes 5 (cells 1-8) and 6 (cells 9-16)
        int byte_idx = (k < 8) ? 5 : 6;
        bool balancing = (alarm_.status[byte_idx] >> (k % 8)) & 1;

        uint8_t va = (k < static_cast<int>(alarm_.cell_volt_alarm.size()))
                     ? alarm_.cell_volt_alarm[k] : 0;

        attr_t extra;
        int cp = cell_color_pair(va, balancing, extra);

        attron(COLOR_PAIR(cp) | extra);

        int bar_len = 0;
        if (v_max > v_min)
            bar_len = static_cast<int>((mv - v_min) / (v_max - v_min) * bar_max);
        bar_len = std::max(0, std::min(bar_len, bar_max));

        mvprintw(row, 0, "  #%2d  %5.3fV  ", k + 1, v);
        for (int b = 0; b < bar_len; ++b) addch(ACS_CKBOARD);
        for (int b = bar_len; b < bar_max; ++b) addch('.');

        const char* lbl = "";
        if      (va & 0x02) lbl = " UVP!";
        else if (va & 0x08) lbl = " OVP!";
        else if (va & 0x01) lbl = " WARN";
        else if (va & 0x04) lbl = " WARN";
        else if (balancing) lbl = " BAL";
        printw("%s", lbl);

        attroff(COLOR_PAIR(cp) | extra);
        ++row;
    }

    // Right column: temperatures and pack stats
    int col2 = left_w + 2;
    int rrow = ROW_CONTENT;

    mvprintw(rrow++, col2, "TEMPERATURES");
    for (int k = 0; k < analog_.temp_count && rrow < LINES - 4; ++k) {
        double tc = analog_.temp_raw[k] / 10.0 - 273.15;
        mvprintw(rrow++, col2, "  Sensor %d  %5.1f\xc2\xb0""C", k + 1, tc);
    }

    ++rrow;
    mvprintw(rrow++, col2, "PACK");
    double total_v  = analog_.total_volt_mv / 1000.0;
    double curr_a   = analog_.current_10ma / 100.0;
    double remain   = analog_.remain_cap_mah / 1000.0;
    double full_cap = analog_.full_cap_mah   / 1000.0;
    int soc = (analog_.full_cap_mah > 0)
              ? (analog_.remain_cap_mah * 100 / analog_.full_cap_mah) : 0;
    const char* dir = (curr_a > 0.05)  ? " (charging)"    :
                      (curr_a < -0.05) ? " (discharging)" : "";

    mvprintw(rrow++, col2, "  Voltage  %6.2f V", total_v);
    mvprintw(rrow++, col2, "  Current  %+6.2f A%s", curr_a, dir);
    mvprintw(rrow++, col2, "  Remain   %5.1f Ah", remain);
    mvprintw(rrow++, col2, "  Full cap %5.1f Ah", full_cap);
    mvprintw(rrow++, col2, "  SOC      %3d%%", soc);
    mvprintw(rrow++, col2, "  Cycles   %d", static_cast<int>(analog_.cycle));
}
void TUI::draw_alarms() {
    int row = ROW_CONTENT;
    int cols = COLS;
    int col2 = cols / 2;

    auto print_status = [&](int r, int c, const char* label, bool ok) {
        mvprintw(r, c, "  %-22s", label);
        if (ok) {
            attron(COLOR_PAIR(CP_OK) | A_BOLD);
            printw("OK");
            attroff(COLOR_PAIR(CP_OK) | A_BOLD);
        } else {
            attron(COLOR_PAIR(CP_UVP_FAULT) | A_BOLD);
            printw("TRIGGERED");
            attroff(COLOR_PAIR(CP_UVP_FAULT) | A_BOLD);
        }
    };

    auto print_mos = [&](int r, int c, const char* label, bool on) {
        mvprintw(r, c, "  %-12s", label);
        if (on) {
            attron(COLOR_PAIR(CP_OK) | A_BOLD);
            printw("● ON");
            attroff(COLOR_PAIR(CP_OK) | A_BOLD);
        } else {
            attron(A_DIM);
            printw("○ OFF");
            attroff(A_DIM);
        }
    };

    // ── Left: MOSFET status + protection events ──
    mvprintw(row++, 0, "  MOSFET STATUS");
    bool chg_on  = (alarm_.status[2] & 0x02) != 0;
    bool dchg_on = (alarm_.status[2] & 0x04) != 0;
    bool bal_on  = false;
    for (int b = 0; b < 2; ++b)
        if (alarm_.status[5 + b]) { bal_on = true; break; }
    print_mos(row++, 0, "Charge",    chg_on);
    print_mos(row++, 0, "Discharge", dchg_on);
    print_mos(row++, 0, "Balance",   bal_on);

    ++row;
    mvprintw(row++, 0, "  PROTECTION EVENTS");
    uint8_t s0 = alarm_.status[0];
    uint8_t s1 = alarm_.status[1];
    print_status(row++, 0, "Cell OVP",      !(s0 & 0x01));
    print_status(row++, 0, "Cell UVP",      !(s0 & 0x02));
    print_status(row++, 0, "Pack OVP",      !(s0 & 0x04));
    print_status(row++, 0, "Pack UVP",      !(s0 & 0x08));
    print_status(row++, 0, "Chg OCP",       !(s0 & 0x10));
    print_status(row++, 0, "Dchg OCP",      !(s0 & 0x20));
    print_status(row++, 0, "Short circuit", !(s0 & 0x40));
    print_status(row++, 0, "Chg Overvolt",  !(s0 & 0x80));
    print_status(row++, 0, "Chg OTP",       !(s1 & 0x01));
    print_status(row++, 0, "Dchg OTP",      !(s1 & 0x04));
    print_status(row++, 0, "MOS OTP",       !(s1 & 0x40));
    print_status(row++, 0, "Chg Under-T",   !(s1 & 0x02));
    print_status(row++, 0, "Dchg Under-T",  !(s1 & 0x08));

    // ── Right: per-cell alarm grid ──
    int rrow = ROW_CONTENT;
    mvprintw(rrow++, col2, "  CELL ALARMS");

    for (int k = 0; k < alarm_.cell_count && rrow < LINES - 2; ++k) {
        int bi = (k < 8) ? 5 : 6;
        bool bal = (alarm_.status[bi] >> (k % 8)) & 1;
        uint8_t va = (k < static_cast<int>(alarm_.cell_volt_alarm.size()))
                     ? alarm_.cell_volt_alarm[k] : 0;

        const char* lbl;
        int cp;
        attr_t ex = A_NORMAL;
        if      (va & 0x02) { lbl = "UVP!";     cp = CP_UVP_FAULT; ex = A_BOLD; }
        else if (va & 0x08) { lbl = "OVP!";     cp = CP_OVP_FAULT; ex = A_BOLD; }
        else if (va & 0x01) { lbl = "UVP warn"; cp = CP_UVP_WARN;  ex = A_BOLD; }
        else if (va & 0x04) { lbl = "OVP warn"; cp = CP_OVP_WARN;  }
        else if (bal)        { lbl = "BAL";      cp = CP_BALANCE;   }
        else                 { lbl = "OK";       cp = CP_OK;        }

        mvprintw(rrow, col2, "  #%2d  ", k + 1);
        attron(COLOR_PAIR(cp) | ex);
        printw("%-8s", lbl);
        attroff(COLOR_PAIR(cp) | ex);
        ++rrow;
    }

    // Hardware faults
    ++rrow;
    if (rrow < LINES - 2) mvprintw(rrow++, col2, "  HARDWARE FAULTS");
    uint8_t s4 = alarm_.status[4];
    if (rrow < LINES - 2) print_status(rrow++, col2, "Charge MOS",    !(s4 & 0x01));
    if (rrow < LINES - 2) print_status(rrow++, col2, "Discharge MOS", !(s4 & 0x02));
    if (rrow < LINES - 2) print_status(rrow++, col2, "Temp sensor",   !(s4 & 0x04));
    if (rrow < LINES - 2) print_status(rrow++, col2, "Cell fault",    !(s4 & 0x10));
    if (rrow < LINES - 2) print_status(rrow++, col2, "Sampling",      !(s4 & 0x20));
}
void TUI::draw_params() {
    if (param_rows_.empty()) {
        mvprintw(ROW_CONTENT, 2, "Loading parameters...");
        return;
    }

    int visible = content_rows();
    int total   = static_cast<int>(param_rows_.size());

    // Clamp scroll so cursor is visible
    if (param_cursor_ < param_scroll_) param_scroll_ = param_cursor_;
    if (param_cursor_ >= param_scroll_ + visible) param_scroll_ = param_cursor_ - visible + 1;
    param_scroll_ = std::max(0, std::min(param_scroll_, total - visible));

    for (int i = 0; i < visible && (param_scroll_ + i) < total; ++i) {
        int idx = param_scroll_ + i;
        int row = ROW_CONTENT + i;
        const ParamRow& pr = param_rows_[idx];

        bool is_cursor = (idx == param_cursor_ && pr.kind != ParamRow::Kind::HEADING);
        bool is_editing = is_cursor && edit_mode_;

        if (pr.kind == ParamRow::Kind::HEADING) {
            attron(A_BOLD | A_UNDERLINE);
            mvprintw(row, 0, "%-*s", COLS, pr.label.c_str());
            attroff(A_BOLD | A_UNDERLINE);
            continue;
        }

        // Check if this row is dirty
        bool row_dirty = false;
        if (pr.group_id >= 0) {
            bool* flags[] = {
                &dirty_.cell_ovp, &dirty_.pack_ovp, &dirty_.chg_ocp, &dirty_.dchg_ocp,
                &dirty_.scp, &dirty_.chg_otp, &dirty_.mosfet_otp, &dirty_.utp,
                &dirty_.balance, &dirty_.pack_b, &dirty_.equalization,
                &dirty_.chg_temp, &dirty_.dchg_temp, &dirty_.mos_chg_temp, &dirty_.mos_dchg_temp
            };
            if (pr.group_id < static_cast<int>(std::size(flags))) row_dirty = *flags[pr.group_id];
        }

        if (is_cursor) attron(COLOR_PAIR(CP_SELECTED));

        // Label
        mvprintw(row, 0, "%-18s", pr.label.c_str());

        // Value
        std::string val_str;
        if (is_editing) {
            val_str = edit_buf_ + "_";
        } else {
            if (pr.kind == ParamRow::Kind::BOOL) {
                val_str = *pr.bval ? "ON" : "OFF";
            } else if (pr.kind == ParamRow::Kind::DOUBLE) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(pr.precision) << *pr.dval;
                val_str = oss.str();
            } else {
                val_str = std::to_string(*pr.ival);
            }
        }

        // Print value — if dirty and not currently selected or editing, highlight yellow
        bool show_dirty = row_dirty && !is_cursor && !is_editing;
        if (show_dirty) {
            attron(COLOR_PAIR(CP_OVP_WARN));
        }
        mvprintw(row, 18, "%-12s %s", val_str.c_str(), pr.unit.c_str());
        if (show_dirty) {
            attroff(COLOR_PAIR(CP_OVP_WARN));
        } else if (is_cursor) {
            attroff(COLOR_PAIR(CP_SELECTED));
        }
    }
}

void TUI::refresh_live_data() {
    std::string err;
    bool ok = bms_.get_analog(static_cast<uint8_t>(current_pack_), analog_, err);
    ok = bms_.get_alarm(static_cast<uint8_t>(current_pack_), alarm_, err) && ok;
    if (ok)
        last_refresh_ = std::chrono::steady_clock::now();
    else if (!err.empty())
        footer_msg_ = err;
}
bool TUI::load_params(std::string& err) {
    uint8_t addr = static_cast<uint8_t>(current_pack_);
    if (!bms_.get_cell_ovp(addr,   params_.cell_ovp,   err)) return false;
    if (!bms_.get_pack_ovp(addr,   params_.pack_ovp,   err)) return false;
    if (!bms_.get_chg_ocp(addr,    params_.chg_ocp,    err)) return false;
    if (!bms_.get_dchg_ocp(addr,   params_.dchg_ocp,   err)) return false;
    if (!bms_.get_scp(             params_.scp,         err)) return false;
    if (!bms_.get_cell_chg_otp(    params_.chg_otp,     err)) return false;
    if (!bms_.get_mosfet_otp(      params_.mosfet_otp,  err)) return false;
    if (!bms_.get_utp(             params_.utp,         err)) return false;
    if (!bms_.get_balance(         params_.balance,     err)) return false;
    if (!bms_.get_pack_b(          params_.pack_b,      err)) return false;
    if (!bms_.get_equalization(    params_.equalization, err)) return false;
    if (!bms_.get_chg_temp_prot(   params_.chg_temp,    err)) return false;
    if (!bms_.get_dchg_temp_prot(  params_.dchg_temp,   err)) return false;
    bms_.get_mos_chg_temp(         params_.mos_chg_temp, err);  // optional
    bms_.get_mos_dchg_temp(        params_.mos_dchg_temp,err);  // optional

    params_orig_ = params_;
    dirty_.clear();
    build_param_rows();
    return true;
}
bool TUI::save_dirty_params(std::string& err) {
    AllParams& p = params_;
    if (dirty_.cell_ovp    && !bms_.set_cell_ovp(p.cell_ovp, err))           return false;
    if (dirty_.pack_ovp    && !bms_.set_pack_ovp(p.pack_ovp, err))           return false;
    if (dirty_.chg_ocp     && !bms_.set_chg_ocp(p.chg_ocp, err))            return false;
    if (dirty_.dchg_ocp    && !bms_.set_dchg_ocp(p.dchg_ocp, err))          return false;
    if (dirty_.scp         && !bms_.set_scp(p.scp, err))                    return false;
    if (dirty_.chg_otp     && !bms_.set_cell_chg_otp(p.chg_otp, err))       return false;
    if (dirty_.mosfet_otp  && !bms_.set_mosfet_otp(p.mosfet_otp, err))      return false;
    if (dirty_.utp         && !bms_.set_utp(p.utp, err))                    return false;
    if (dirty_.balance     && !bms_.set_balance(p.balance, err))             return false;
    if (dirty_.pack_b      && !bms_.set_pack_b(p.pack_b, err))              return false;
    if (dirty_.equalization && !bms_.set_equalization(p.equalization, err))  return false;
    if (dirty_.chg_temp    && !bms_.set_chg_temp_prot(p.chg_temp, err))     return false;
    if (dirty_.dchg_temp   && !bms_.set_dchg_temp_prot(p.dchg_temp, err))   return false;
    if (dirty_.mos_chg_temp  && p.mos_chg_temp.present  &&
        !bms_.set_mos_chg_temp(p.mos_chg_temp, err))                        return false;
    if (dirty_.mos_dchg_temp && p.mos_dchg_temp.present &&
        !bms_.set_mos_dchg_temp(p.mos_dchg_temp, err))                      return false;
    return true;
}
void TUI::revert_params() {
    params_ = params_orig_;
    dirty_.clear();
    build_param_rows();  // REQUIRED: rebuilds pointers into refreshed params_
}
void TUI::switch_pack(int delta) {
    if (pack_count_ <= 1) return;

    if (dirty_.any()) {
        footer_msg_ = "Unsaved changes — press s to save or Esc to discard";
        return;
    }

    int next = current_pack_ + delta;
    if (next < 1) next = pack_count_;
    if (next > pack_count_) next = 1;
    current_pack_ = next;

    std::string err;
    refresh_live_data();
    bool ok = load_params(err);
    param_cursor_ = 0;
    // Advance past heading rows
    while (param_cursor_ < static_cast<int>(param_rows_.size()) &&
           param_rows_[param_cursor_].kind == ParamRow::Kind::HEADING)
        ++param_cursor_;
    edit_mode_ = false;
    if (ok)
        footer_msg_.clear();
    else
        footer_msg_ = err;
}
void TUI::handle_params_key(int ch) {
    if (param_rows_.empty()) return;

    // Skip heading rows when moving cursor
    auto next_editable = [&](int from, int dir) -> int {
        int n = static_cast<int>(param_rows_.size());
        int idx = from + dir;
        while (idx >= 0 && idx < n && param_rows_[idx].kind == ParamRow::Kind::HEADING)
            idx += dir;
        if (idx < 0 || idx >= n) return from;
        return idx;
    };

    if (edit_mode_) {
        // ── In edit mode ──
        if (ch == 27) {  // Esc — cancel edit
            edit_mode_ = false;
            edit_buf_.clear();
            return;
        }
        if (ch == '\n' || ch == KEY_ENTER || ch == KEY_UP || ch == KEY_DOWN) {
            // Commit the edit
            const ParamRow& pr = param_rows_[param_cursor_];
            if (!edit_buf_.empty()) {
                if (pr.kind == ParamRow::Kind::DOUBLE) {
                    try { *pr.dval = std::stod(edit_buf_); } catch (...) {}
                } else if (pr.kind == ParamRow::Kind::INT) {
                    try { *pr.ival = std::stoi(edit_buf_); } catch (...) {}
                }
                // Mark group dirty
                mark_dirty(pr.group_id);
            }
            edit_mode_ = false;
            edit_buf_.clear();
            // Move cursor if arrow was pressed
            if (ch == KEY_UP)   param_cursor_ = next_editable(param_cursor_, -1);
            if (ch == KEY_DOWN) param_cursor_ = next_editable(param_cursor_, +1);
            return;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (!edit_buf_.empty()) edit_buf_.pop_back();
            return;
        }
        if (isdigit(ch) || ch == '.' || ch == '-') {
            edit_buf_ += static_cast<char>(ch);
        }
        return;
    }

    // ── Normal (non-edit) mode ──
    if (ch == KEY_UP)   { param_cursor_ = next_editable(param_cursor_, -1); footer_msg_.clear(); return; }
    if (ch == KEY_DOWN) { param_cursor_ = next_editable(param_cursor_, +1); footer_msg_.clear(); return; }
    if (ch == KEY_PPAGE) {
        for (int i = 0; i < content_rows() / 2; ++i)
            param_cursor_ = next_editable(param_cursor_, -1);
        footer_msg_.clear();
        return;
    }
    if (ch == KEY_NPAGE) {
        for (int i = 0; i < content_rows() / 2; ++i)
            param_cursor_ = next_editable(param_cursor_, +1);
        footer_msg_.clear();
        return;
    }

    const ParamRow& pr = param_rows_[param_cursor_];

    if (ch == ' ' && pr.kind == ParamRow::Kind::BOOL) {
        *pr.bval = !(*pr.bval);
        mark_dirty(pr.group_id);
        return;
    }

    if ((isdigit(ch) || ch == '.' || ch == '-') &&
        (pr.kind == ParamRow::Kind::DOUBLE || pr.kind == ParamRow::Kind::INT)) {
        edit_mode_ = true;
        edit_buf_.clear();
        edit_buf_ += static_cast<char>(ch);
        return;
    }

    if (ch == 's' || ch == 'S') {
        if (dirty_.any()) {
            std::string err;
            footer_msg_ = "Saving...";
            draw_footer();
            ::refresh();
            if (save_dirty_params(err)) {
                footer_msg_ = "Saved.";
                params_orig_ = params_;
                dirty_.clear();
            } else {
                footer_msg_ = "Save error: " + err;
            }
        }
        return;
    }

    if (ch == 27) {  // Esc — revert
        revert_params();
        footer_msg_ = "Reverted.";
        return;
    }
}
void TUI::mark_dirty(int group_id) {
    bool* flags[] = {
        &dirty_.cell_ovp, &dirty_.pack_ovp, &dirty_.chg_ocp, &dirty_.dchg_ocp,
        &dirty_.scp, &dirty_.chg_otp, &dirty_.mosfet_otp, &dirty_.utp,
        &dirty_.balance, &dirty_.pack_b, &dirty_.equalization,
        &dirty_.chg_temp, &dirty_.dchg_temp, &dirty_.mos_chg_temp, &dirty_.mos_dchg_temp
    };
    if (group_id >= 0 && group_id < static_cast<int>(std::size(flags)))
        *flags[group_id] = true;
}
void TUI::build_param_rows() {
    param_rows_.clear();
    auto& p = params_;  // current values

    auto heading = [&](const char* title) {
        param_rows_.push_back({ParamRow::Kind::HEADING, title, "", nullptr, nullptr, nullptr, -1});
    };
    auto pb = [&](const char* lbl, bool* v, int gid) {
        param_rows_.push_back({ParamRow::Kind::BOOL, lbl, "", v, nullptr, nullptr, gid});
    };
    auto pd = [&](const char* lbl, double* v, int gid, const char* unit, int prec=3) {
        ParamRow r{ParamRow::Kind::DOUBLE, lbl, unit, nullptr, v, nullptr, gid};
        r.precision = prec;
        param_rows_.push_back(r);
    };
    auto pi = [&](const char* lbl, int* v, int gid, const char* unit) {
        param_rows_.push_back({ParamRow::Kind::INT, lbl, unit, nullptr, nullptr, v, gid});
    };

    heading("Cell Over-Voltage Protection");
    pb("  Enable",   &p.cell_ovp.enable,    0);
    pd("  Protect",  &p.cell_ovp.protect_v, 0, "V");
    pd("  Alarm",    &p.cell_ovp.alarm_v,   0, "V");
    pd("  Recover",  &p.cell_ovp.recover_v, 0, "V");
    pi("  Delay",    &p.cell_ovp.delay_ms,  0, "ms");

    heading("Pack Over-Voltage Protection");
    pb("  Enable",   &p.pack_ovp.enable,    1);
    pd("  Protect",  &p.pack_ovp.protect_v, 1, "V");
    pd("  Alarm",    &p.pack_ovp.alarm_v,   1, "V");
    pd("  Recover",  &p.pack_ovp.recover_v, 1, "V");
    pi("  Delay",    &p.pack_ovp.delay_ms,  1, "ms");

    heading("Charge Over-Current Protection");
    pb("  Enable",   &p.chg_ocp.enable,    2);
    pd("  Protect",  &p.chg_ocp.protect_a, 2, "A", 1);
    pd("  Alarm",    &p.chg_ocp.alarm_a,   2, "A", 1);
    pd("  Recover",  &p.chg_ocp.recover_a, 2, "A", 1);
    pi("  Delay",    &p.chg_ocp.delay_ms,  2, "ms");

    heading("Discharge Over-Current Protection");
    pb("  Enable",   &p.dchg_ocp.enable,    3);
    pd("  Protect",  &p.dchg_ocp.protect_a, 3, "A", 1);
    pd("  Alarm",    &p.dchg_ocp.alarm_a,   3, "A", 1);
    pd("  Recover",  &p.dchg_ocp.recover_a, 3, "A", 1);
    pi("  Delay",    &p.dchg_ocp.delay_ms,  3, "ms");

    heading("Short-Circuit Protection");
    pb("  Enable",    &p.scp.enable,    4);
    pi("  Threshold", &p.scp.threshold, 4, "");
    pi("  Time",      &p.scp.time_us,   4, "us");
    pi("  Delay",     &p.scp.delay_ms,  4, "ms");

    heading("Charge Over-Temperature Protection");
    pb("  Enable",   &p.chg_otp.enable,    5);
    pi("  Trigger",  &p.chg_otp.trigger_c, 5, "\xc2\xb0""C");
    pi("  Recover",  &p.chg_otp.recover_c, 5, "\xc2\xb0""C");
    pi("  Delay",    &p.chg_otp.delay_ms,  5, "ms");

    heading("MOSFET Over-Temperature Protection");
    pi("  Trigger",  &p.mosfet_otp.trigger_c, 6, "\xc2\xb0""C");
    pi("  Delay",    &p.mosfet_otp.delay_ms,  6, "ms");

    heading("Under-Temperature Protection");
    pi("  Delay",    &p.utp.delay_ms, 7, "ms");

    heading("Balance (Params A)");
    pd("  Threshold", &p.balance.threshold_v, 8, "V");
    pi("  Delta",     &p.balance.delta_mv,    8, "mV");

    heading("Pack Params B");
    pd("  Voltage",  &p.pack_b.volt_v, 9, "V");
    pi("  Value",    &p.pack_b.value,  9, "");

    heading("Equalization");
    pd("  Start V",  &p.equalization.start_v,       10, "V");
    pi("  Delta",    &p.equalization.delta_pack_mv,  10, "mV");
    pi("  Cells",    &p.equalization.cell_cnt,       10, "");

    heading("Charge Temperature Protection");
    pb("  Enable",   &p.chg_temp.enable, 11);
    for (int k = 0; k < 6; ++k) {
        std::string lbl = "  Sensor " + std::to_string(k+1);
        pi(lbl.c_str(), &p.chg_temp.temp[k], 11, "\xc2\xb0""C");
    }

    heading("Discharge Temperature Protection");
    pb("  Enable",   &p.dchg_temp.enable, 12);
    for (int k = 0; k < 6; ++k) {
        std::string lbl = "  Sensor " + std::to_string(k+1);
        pi(lbl.c_str(), &p.dchg_temp.temp[k], 12, "\xc2\xb0""C");
    }

    if (p.mos_chg_temp.present) {
        heading("MOS Charge Temperature");
        pb("  Enable",   &p.mos_chg_temp.enable, 13);
        for (int k = 0; k < 3; ++k) {
            std::string lbl = "  Sensor " + std::to_string(k+1);
            pi(lbl.c_str(), &p.mos_chg_temp.temp[k], 13, "\xc2\xb0""C");
        }
    }

    if (p.mos_dchg_temp.present) {
        heading("MOS Discharge Temperature");
        pb("  Enable",   &p.mos_dchg_temp.enable, 14);
        for (int k = 0; k < 6; ++k) {
            std::string lbl = "  Sensor " + std::to_string(k+1);
            pi(lbl.c_str(), &p.mos_dchg_temp.temp[k], 14, "\xc2\xb0""C");
        }
    }
}

int TUI::content_rows() const { return LINES - ROW_CONTENT - 1; }
int TUI::content_cols() const { return COLS; }

} // namespace pace
