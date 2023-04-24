/*
 * Helper function to remove the border around a dialog item such as
 * a read-only edit control.
 */

#include "putty.h"

void MakeDlgItemBorderless(HWND parent, int id)
{
    HWND child = GetDlgItem(parent, id);
    LONG_PTR style = GetWindowLongPtr(child, GWL_STYLE);
    LONG_PTR exstyle = GetWindowLongPtr(child, GWL_EXSTYLE);
    style &= ~WS_BORDER;
    exstyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    SetWindowLongPtr(child, GWL_STYLE, style);
    SetWindowLongPtr(child, GWL_EXSTYLE, exstyle);
    SetWindowPos(child, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
