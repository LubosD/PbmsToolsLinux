#pragma once
#include "bms.h"
#include <chrono>
#include <string>
#include <vector>

namespace pace {

// Aggregate of every param struct — held as current (editable) + original (for revert).
struct AllParams {
    CellVoltProtParams cell_ovp;
    CellVoltProtParams pack_ovp;
    CurrProtParams     chg_ocp;
    CurrProtParams     dchg_ocp;
    ShortCircuitParams scp;
    CellTempProtParams chg_otp;
    MosfetOtpParams    mosfet_otp;
    UnderTempParams    utp;
    BalanceParams      balance;
    PackParamsBParams  pack_b;
    EqualizationParams equalization;
    TempProtGroup      chg_temp;
    TempProtGroup      dchg_temp;
    TempProtGroup      mos_chg_temp;
    TempProtGroup      mos_dchg_temp;
};

// Dirty flags — one per write-able group.
struct DirtyFlags {
    bool cell_ovp=false, pack_ovp=false;
    bool chg_ocp=false, dchg_ocp=false;
    bool scp=false, chg_otp=false;
    bool mosfet_otp=false, utp=false;
    bool balance=false, pack_b=false;
    bool equalization=false;
    bool chg_temp=false, dchg_temp=false;
    bool mos_chg_temp=false, mos_dchg_temp=false;
    bool any() const {
        return cell_ovp||pack_ovp||chg_ocp||dchg_ocp||scp||chg_otp||
               mosfet_otp||utp||balance||pack_b||equalization||
               chg_temp||dchg_temp||mos_chg_temp||mos_dchg_temp;
    }
    void clear() { *this = DirtyFlags{}; }
};

// A single editable row in the params tab.
struct ParamRow {
    enum class Kind { BOOL, DOUBLE, INT, HEADING };
    Kind        kind;
    std::string label;
    std::string unit;
    // Pointers into AllParams current_ fields (null for HEADING rows)
    bool*   bval = nullptr;
    double* dval = nullptr;
    int*    ival = nullptr;
    int     group_id = -1;  // maps to DirtyFlags field; -1 = not editable
    double  precision = 3;  // decimal places for display of doubles
};

class TUI {
public:
    TUI(BMS& bms, const std::string& port);
    ~TUI();
    void run();

private:
    BMS&        bms_;
    std::string port_;
    int         pack_count_   = 1;
    int         current_pack_ = 1;
    int         current_tab_  = 0;   // 0=Analog 1=Alarms 2=Params
    bool        auto_refresh_ = true;

    AnalogData  analog_;
    AlarmData   alarm_;
    AllParams   params_;
    AllParams   params_orig_;
    DirtyFlags  dirty_;

    // Params tab cursor & editing
    int         param_cursor_  = 0;
    int         param_scroll_  = 0;
    bool        edit_mode_     = false;
    std::string edit_buf_;
    std::vector<ParamRow> param_rows_;

    // Footer message and last-refresh timestamp
    std::string footer_msg_;
    std::chrono::steady_clock::time_point last_refresh_;

    void init_ncurses();
    void draw_all();
    void draw_header();
    void draw_tabs();
    void draw_content();
    void draw_analog();
    void draw_alarms();
    void draw_params();
    void draw_footer();

    void refresh_live_data();
    bool load_params(std::string& err);
    bool save_dirty_params(std::string& err);
    void revert_params();
    void switch_pack(int delta);
    void handle_params_key(int ch);
    void build_param_rows();

    int  content_rows() const;
    int  content_cols() const;
};

} // namespace pace
