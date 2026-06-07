#include "tui.h"
#include <ncurses.h>

namespace pace {

TUI::TUI(BMS& bms, const std::string& port)
    : bms_(bms), port_(port) {}

TUI::~TUI() {
    if (isendwin() == FALSE) endwin();
}

void TUI::run() {
    init_ncurses();
    mvprintw(0, 0, "TUI placeholder — press q to quit");
    refresh();
    int ch;
    while ((ch = getch()) != 'q' && ch != 'Q') {}
    endwin();
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
    }
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

int TUI::content_rows() const { return LINES - 4; }
int TUI::content_cols() const { return COLS; }

} // namespace pace
