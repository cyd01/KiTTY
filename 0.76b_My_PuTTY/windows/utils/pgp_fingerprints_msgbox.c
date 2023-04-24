/*
 * Display the fingerprints of the PGP Master Keys to the user as a
 * GUI message box.
 */

#include "putty.h"

void pgp_fingerprints_msgbox(HWND owner)
{
    message_box(
        owner,
        "These are the fingerprints of the PuTTY PGP Master Keys. They can\n"
        "be used to establish a trust path from this executable to another\n"
        "one. See the manual for more information.\n"
        "(Note: these fingerprints have nothing to do with SSH!)\n"
        "\n"
        "PuTTY Master Key as of " PGP_MASTER_KEY_YEAR
        " (" PGP_MASTER_KEY_DETAILS "):\n"
        "  " PGP_MASTER_KEY_FP "\n\n"
        "Previous Master Key (" PGP_PREV_MASTER_KEY_YEAR
        ", " PGP_PREV_MASTER_KEY_DETAILS "):\n"
        "  " PGP_PREV_MASTER_KEY_FP,
        "PGP fingerprints", MB_ICONINFORMATION | MB_OK,
        HELPCTXID(pgp_fingerprints));
}
