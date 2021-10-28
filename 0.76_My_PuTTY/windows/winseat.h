/*
 * Small implementation of Seat and LogPolicy shared between window.c
 * and windlg.c.
 */

typedef struct WinGuiSeat WinGuiSeat;

struct WinGuiSeat {
    HWND term_hwnd;
    Seat seat;
    LogPolicy logpolicy;
};

extern const LogPolicyVtable win_gui_logpolicy_vt; /* in windlg.c */
