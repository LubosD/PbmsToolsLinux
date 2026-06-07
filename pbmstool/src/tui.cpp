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
            "↑↓ navigate  digits: edit  Space: toggle  s: save  Esc: revert");
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

void TUI::draw_analog() {
    mvprintw(ROW_CONTENT, 2, "[Analog data — not yet implemented]");
}
void TUI::draw_alarms() {
    mvprintw(ROW_CONTENT, 2, "[Alarm data — not yet implemented]");
}
void TUI::draw_params() {
    mvprintw(ROW_CONTENT, 2, "[Parameters — not yet implemented]");
}

void TUI::refresh_live_data() {
    std::string err;
    bms_.get_analog(static_cast<uint8_t>(current_pack_), analog_, err);
    bms_.get_alarm (static_cast<uint8_t>(current_pack_), alarm_,  err);
    last_refresh_ = std::chrono::steady_clock::now();
}
bool TUI::load_params(std::string&) { return true; }
bool TUI::save_dirty_params(std::string&) { return true; }
void TUI::revert_params() {}
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
    load_params(err);
    param_cursor_ = 0;
    // Advance past heading rows
    while (param_cursor_ < static_cast<int>(param_rows_.size()) &&
           param_rows_[param_cursor_].kind == ParamRow::Kind::HEADING)
        ++param_cursor_;
    edit_mode_ = false;
    footer_msg_.clear();
}
void TUI::handle_params_key(int) {}
void TUI::build_param_rows() {}

int TUI::content_rows() const { return LINES - ROW_CONTENT - 1; }
int TUI::content_cols() const { return COLS; }

} // namespace pace
