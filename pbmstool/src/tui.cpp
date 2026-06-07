#include "tui.h"
#include <ncurses.h>
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

void TUI::draw_all()     {}
void TUI::draw_header()  {}
void TUI::draw_tabs()    {}
void TUI::draw_content() {}
void TUI::draw_analog()  {}
void TUI::draw_alarms()  {}
void TUI::draw_params()  {}
void TUI::draw_footer()  {}

void TUI::refresh_live_data() {}
bool TUI::load_params(std::string&) { return true; }
bool TUI::save_dirty_params(std::string&) { return true; }
void TUI::revert_params() {}
void TUI::switch_pack(int) {}
void TUI::handle_params_key(int) {}
void TUI::build_param_rows() {}

int TUI::content_rows() const { return LINES - ROW_CONTENT - 1; }
int TUI::content_cols() const { return COLS; }

} // namespace pace
