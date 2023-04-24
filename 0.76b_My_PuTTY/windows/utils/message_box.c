/*
 * Message box with optional context help.
 */

#include "putty.h"

static HWND message_box_owner;

/* Callback function to launch context help. */
static VOID CALLBACK message_box_help_callback(LPHELPINFO lpHelpInfo)
{
    const char *context = NULL;
#define CHECK_CTX(name) \
    do { \
        if (lpHelpInfo->dwContextId == WINHELP_CTXID_ ## name) \
            context = WINHELP_CTX_ ## name; \
    } while (0)
    CHECK_CTX(errors_hostkey_absent);
    CHECK_CTX(errors_hostkey_changed);
    CHECK_CTX(errors_cantloadkey);
    CHECK_CTX(option_cleanup);
    CHECK_CTX(pgp_fingerprints);
#undef CHECK_CTX
    if (context)
        launch_help(message_box_owner, context);
}

int message_box(HWND owner, LPCTSTR text, LPCTSTR caption,
                DWORD style, DWORD helpctxid)
{
    MSGBOXPARAMS mbox;

    /*
     * We use MessageBoxIndirect() because it allows us to specify a
     * callback function for the Help button.
     */
    mbox.cbSize = sizeof(mbox);
    /* Assumes the globals `hinst' and `hwnd' have sensible values. */
    mbox.hInstance = hinst;
    mbox.hwndOwner = message_box_owner = owner;
    mbox.lpfnMsgBoxCallback = &message_box_help_callback;
    mbox.dwLanguageId = LANG_NEUTRAL;
    mbox.lpszText = text;
    mbox.lpszCaption = caption;
    mbox.dwContextHelpId = helpctxid;
    mbox.dwStyle = style;
    if (helpctxid != 0 && has_help()) mbox.dwStyle |= MB_HELP;
    return MessageBoxIndirect(&mbox);
}
