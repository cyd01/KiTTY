/*
 * PuTTY key generation front end (Windows).
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define PUTTY_DO_GLOBALS

#include "putty.h"
#include "ssh.h"
#include "licence.h"
#include "winsecur.h"

#include <commctrl.h>

#ifdef MSVC4
#define ICON_BIG        1
#endif

#define WM_DONEKEY (WM_APP + 1)

#define DEFAULT_KEY_BITS 2048
#define DEFAULT_CURVE_INDEX 0

static char *cmdline_keyfile = NULL;

#ifndef INTEGRATED_KEYGEN
int GetZModemFlag() { return 0 ; }
/*
 * Print a modal (Really Bad) message box and perform a fatal exit.
 */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char *stuff;

    va_start(ap, fmt);
    stuff = dupvprintf(fmt, ap);
    va_end(ap);
    MessageBox(NULL, stuff, "PuTTYgen Fatal Error",
	       MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(stuff);
    exit(1);
}

/*
 * Print a non-fatal message box and do not exit.
 */
void nonfatal(const char *fmt, ...)
{
    va_list ap;
    char *stuff;

    va_start(ap, fmt);
    stuff = dupvprintf(fmt, ap);
    va_end(ap);
    MessageBox(NULL, stuff, "PuTTYgen Error",
	       MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(stuff);
}
#endif

/* ----------------------------------------------------------------------
 * Progress report code. This is really horrible :-)
 */
#define PROGRESSRANGE 65535
#define MAXPHASE 5
struct progress {
    int nphases;
    struct {
	bool exponential;
	unsigned startpoint, total;
	unsigned param, current, n;    /* if exponential */
	unsigned mult;		       /* if linear */
    } phases[MAXPHASE];
    unsigned total, divisor, range;
    HWND progbar;
};

static void progress_update(void *param, int action, int phase, int iprogress)
{
    struct progress *p = (struct progress *) param;
    unsigned progress = iprogress;
    int position;

    if (action < PROGFN_READY && p->nphases < phase)
	p->nphases = phase;
    switch (action) {
      case PROGFN_INITIALISE:
	p->nphases = 0;
	break;
      case PROGFN_LIN_PHASE:
	p->phases[phase-1].exponential = false;
	p->phases[phase-1].mult = p->phases[phase].total / progress;
	break;
      case PROGFN_EXP_PHASE:
	p->phases[phase-1].exponential = true;
	p->phases[phase-1].param = 0x10000 + progress;
	p->phases[phase-1].current = p->phases[phase-1].total;
	p->phases[phase-1].n = 0;
	break;
      case PROGFN_PHASE_EXTENT:
	p->phases[phase-1].total = progress;
	break;
      case PROGFN_READY:
	{
	    unsigned total = 0;
	    int i;
	    for (i = 0; i < p->nphases; i++) {
		p->phases[i].startpoint = total;
		total += p->phases[i].total;
	    }
	    p->total = total;
	    p->divisor = ((p->total + PROGRESSRANGE - 1) / PROGRESSRANGE);
	    p->range = p->total / p->divisor;
	    SendMessage(p->progbar, PBM_SETRANGE, 0, MAKELPARAM(0, p->range));
	}
	break;
      case PROGFN_PROGRESS:
	if (p->phases[phase-1].exponential) {
	    while (p->phases[phase-1].n < progress) {
		p->phases[phase-1].n++;
		p->phases[phase-1].current *= p->phases[phase-1].param;
		p->phases[phase-1].current /= 0x10000;
	    }
	    position = (p->phases[phase-1].startpoint +
			p->phases[phase-1].total - p->phases[phase-1].current);
	} else {
	    position = (p->phases[phase-1].startpoint +
			progress * p->phases[phase-1].mult);
	}
	SendMessage(p->progbar, PBM_SETPOS, position / p->divisor, 0);
	break;
    }
}

struct PassphraseProcStruct {
    char **passphrase;
    char *comment;
};

/*
 * Dialog-box function for the passphrase box.
 */
static INT_PTR CALLBACK PassphraseProc(HWND hwnd, UINT msg,
				   WPARAM wParam, LPARAM lParam)
{
    static char **passphrase = NULL;
    struct PassphraseProcStruct *p;

    switch (msg) {
      case WM_INITDIALOG:
	SetForegroundWindow(hwnd);
	SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

	/*
	 * Centre the window.
	 */
	{			       /* centre the window */
	    RECT rs, rd;
	    HWND hw;

	    hw = GetDesktopWindow();
	    if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
		MoveWindow(hwnd,
			   (rs.right + rs.left + rd.left - rd.right) / 2,
			   (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
			   rd.right - rd.left, rd.bottom - rd.top, true);
	}

	p = (struct PassphraseProcStruct *) lParam;
	passphrase = p->passphrase;
	if (p->comment)
	    SetDlgItemText(hwnd, 101, p->comment);
        burnstr(*passphrase);
        *passphrase = dupstr("");
	SetDlgItemText(hwnd, 102, *passphrase);
	return 0;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    if (*passphrase)
		EndDialog(hwnd, 1);
	    else
		MessageBeep(0);
	    return 0;
	  case IDCANCEL:
	    EndDialog(hwnd, 0);
	    return 0;
	  case 102:		       /* edit box */
	    if ((HIWORD(wParam) == EN_CHANGE) && passphrase) {
                burnstr(*passphrase);
                *passphrase = GetDlgItemText_alloc(hwnd, 102);
	    }
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog(hwnd, 0);
	return 0;
    }
    return 0;
}

/*
 * Prompt for a key file. Assumes the filename buffer is of size
 * FILENAME_MAX.
 */
static bool prompt_keyfile(HWND hwnd, char *dlgtitle,
                           char *filename, bool save, bool ppk)
{
    OPENFILENAME of;
    memset(&of, 0, sizeof(of));
    of.hwndOwner = hwnd;
    if (ppk) {
	of.lpstrFilter = "PuTTY Private Key Files (*.ppk)\0*.ppk\0"
	    "All Files (*.*)\0*\0\0\0";
	of.lpstrDefExt = ".ppk";
    } else {
	of.lpstrFilter = "All Files (*.*)\0*\0\0\0";
    }
    of.lpstrCustomFilter = NULL;
    of.nFilterIndex = 1;
    of.lpstrFile = filename;
    *filename = '\0';
    of.nMaxFile = FILENAME_MAX;
    of.lpstrFileTitle = NULL;
    of.lpstrTitle = dlgtitle;
    of.Flags = 0;
    return request_file(NULL, &of, false, save);
}

/*
 * Dialog-box function for the Licence box.
 */
static INT_PTR CALLBACK LicenceProc(HWND hwnd, UINT msg,
				WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG:
	/*
	 * Centre the window.
	 */
	{			       /* centre the window */
	    RECT rs, rd;
	    HWND hw;

	    hw = GetDesktopWindow();
	    if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
		MoveWindow(hwnd,
			   (rs.right + rs.left + rd.left - rd.right) / 2,
			   (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
			   rd.right - rd.left, rd.bottom - rd.top, true);
	}

        SetDlgItemText(hwnd, 1000, LICENCE_TEXT("\r\n\r\n"));
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	  case IDCANCEL:
	    EndDialog(hwnd, 1);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog(hwnd, 1);
	return 0;
    }
    return 0;
}

/*
 * Dialog-box function for the About box.
 */
static INT_PTR CALLBACK AboutProc(HWND hwnd, UINT msg,
			      WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG:
	/*
	 * Centre the window.
	 */
	{			       /* centre the window */
	    RECT rs, rd;
	    HWND hw;

	    hw = GetDesktopWindow();
	    if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
		MoveWindow(hwnd,
			   (rs.right + rs.left + rd.left - rd.right) / 2,
			   (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
			   rd.right - rd.left, rd.bottom - rd.top, true);
	}

        {
            char *buildinfo_text = buildinfo("\r\n");
            char *text = dupprintf
                ("PuTTYgen\r\n\r\n%s\r\n\r\n%s\r\n\r\n%s",
                 ver, buildinfo_text,
                 "\251 " SHORT_COPYRIGHT_DETAILS ". All rights reserved.");
            sfree(buildinfo_text);
            SetDlgItemText(hwnd, 1000, text);
            sfree(text);
        }
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	  case IDCANCEL:
	    EndDialog(hwnd, 1);
	    return 0;
	  case 101:
	    EnableWindow(hwnd, 0);
#ifdef INTEGRATED_KEYGEN
	    DialogBox(hinst, MAKEINTRESOURCE(814), hwnd, LicenceProc);
#else
	    DialogBox(hinst, MAKEINTRESOURCE(214), hwnd, LicenceProc);
#endif
	    EnableWindow(hwnd, 1);
	    SetActiveWindow(hwnd);
	    return 0;
	  case 102:
	    /* Load web browser */
	    ShellExecute(hwnd, "open",
			 "https://www.chiark.greenend.org.uk/~sgtatham/putty/",
			 0, 0, SW_SHOWDEFAULT);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog(hwnd, 1);
	return 0;
    }
    return 0;
}

typedef enum {RSA, DSA, ECDSA, ED25519} keytype;

/*
 * Thread to generate a key.
 */
struct rsa_key_thread_params {
    HWND progressbar;		       /* notify this with progress */
    HWND dialog;		       /* notify this on completion */
    int key_bits;		       /* bits in key modulus (RSA, DSA) */
    int curve_bits;                    /* bits in elliptic curve (ECDSA) */
    keytype keytype;
    union {
        RSAKey *key;
        struct dss_key *dsskey;
        struct ecdsa_key *eckey;
        struct eddsa_key *edkey;
    };
};
static DWORD WINAPI generate_key_thread(void *param)
{
    struct rsa_key_thread_params *params =
	(struct rsa_key_thread_params *) param;
    struct progress prog;
    prog.progbar = params->progressbar;

    progress_update(&prog, PROGFN_INITIALISE, 0, 0);

    if (params->keytype == DSA)
	dsa_generate(params->dsskey, params->key_bits, progress_update, &prog);
    else if (params->keytype == ECDSA)
        ecdsa_generate(params->eckey, params->curve_bits,
                       progress_update, &prog);
    else if (params->keytype == ED25519)
        eddsa_generate(params->edkey, 256, progress_update, &prog);
    else
	rsa_generate(params->key, params->key_bits, progress_update, &prog);

    PostMessage(params->dialog, WM_DONEKEY, 0, 0);

    sfree(params);
    return 0;
}

struct MainDlgState {
    bool collecting_entropy;
    bool generation_thread_exists;
    bool key_exists;
    int entropy_got, entropy_required, entropy_size;
    int key_bits, curve_bits;
    bool ssh2;
    keytype keytype;
    char **commentptr;		       /* points to key.comment or ssh2key.comment */
    ssh2_userkey ssh2key;
    unsigned *entropy;
    union {
        RSAKey key;
        struct dss_key dsskey;
        struct ecdsa_key eckey;
        struct eddsa_key edkey;
    };
    HMENU filemenu, keymenu, cvtmenu;
};

static void hidemany(HWND hwnd, const int *ids, bool hideit)
{
    while (*ids) {
	ShowWindow(GetDlgItem(hwnd, *ids++), (hideit ? SW_HIDE : SW_SHOW));
    }
}

static void setupbigedit1(HWND hwnd, int id, int idstatic, RSAKey *key)
{
    char *buffer = ssh1_pubkey_str(key);
    SetDlgItemText(hwnd, id, buffer);
    SetDlgItemText(hwnd, idstatic,
		   "&Public key for pasting into authorized_keys file:");
    sfree(buffer);
}

static void setupbigedit2(HWND hwnd, int id, int idstatic,
			  ssh2_userkey *key)
{
    char *buffer = ssh2_pubkey_openssh_str(key);
    SetDlgItemText(hwnd, id, buffer);
    SetDlgItemText(hwnd, idstatic, "&Public key for pasting into "
		   "OpenSSH authorized_keys file:");
    sfree(buffer);
}

#ifndef INTEGRATED_KEYGEN
/*
 * Warn about the obsolescent key file format.
 */
void old_keyfile_warning(void)
{
    static const char mbtitle[] = "PuTTY Key File Warning";
    static const char message[] =
	"You are loading an SSH-2 private key which has an\n"
	"old version of the file format. This means your key\n"
	"file is not fully tamperproof. Future versions of\n"
	"PuTTY may stop supporting this private key format,\n"
	"so we recommend you convert your key to the new\n"
	"format.\n"
	"\n"
	"Once the key is loaded into PuTTYgen, you can perform\n"
	"this conversion simply by saving it again.";

    MessageBox(NULL, message, mbtitle, MB_OK);
}
#endif

enum {
    controlidstart = 100,
    IDC_QUIT,
    IDC_TITLE,
    IDC_BOX_KEY,
    IDC_NOKEY,
    IDC_GENERATING,
    IDC_PROGRESS,
    IDC_PKSTATIC, IDC_KEYDISPLAY,
    IDC_FPSTATIC, IDC_FINGERPRINT,
    IDC_COMMENTSTATIC, IDC_COMMENTEDIT,
    IDC_PASSPHRASE1STATIC, IDC_PASSPHRASE1EDIT,
    IDC_PASSPHRASE2STATIC, IDC_PASSPHRASE2EDIT,
    IDC_BOX_ACTIONS,
    IDC_GENSTATIC, IDC_GENERATE,
    IDC_LOADSTATIC, IDC_LOAD,
    IDC_SAVESTATIC, IDC_SAVE, IDC_SAVEPUB,
    IDC_BOX_PARAMS,
    IDC_TYPESTATIC, IDC_KEYSSH1, IDC_KEYSSH2RSA, IDC_KEYSSH2DSA,
    IDC_KEYSSH2ECDSA, IDC_KEYSSH2ED25519,
    IDC_BITSSTATIC, IDC_BITS,
    IDC_CURVESTATIC, IDC_CURVE,
    IDC_NOTHINGSTATIC,
    IDC_ABOUT,
    IDC_GIVEHELP,
    IDC_IMPORT,
    IDC_EXPORT_OPENSSH_AUTO, IDC_EXPORT_OPENSSH_NEW,
    IDC_EXPORT_SSHCOM
};

static const int nokey_ids[] = { IDC_NOKEY, 0 };
static const int generating_ids[] = { IDC_GENERATING, IDC_PROGRESS, 0 };
static const int gotkey_ids[] = {
    IDC_PKSTATIC, IDC_KEYDISPLAY,
    IDC_FPSTATIC, IDC_FINGERPRINT,
    IDC_COMMENTSTATIC, IDC_COMMENTEDIT,
    IDC_PASSPHRASE1STATIC, IDC_PASSPHRASE1EDIT,
    IDC_PASSPHRASE2STATIC, IDC_PASSPHRASE2EDIT, 0
};

/*
 * Small UI helper function to switch the state of the main dialog
 * by enabling and disabling controls and menu items.
 */
void ui_set_state(HWND hwnd, struct MainDlgState *state, int status)
{
    int type;

    switch (status) {
      case 0:			       /* no key */
	hidemany(hwnd, nokey_ids, false);
	hidemany(hwnd, generating_ids, true);
	hidemany(hwnd, gotkey_ids, true);
	EnableWindow(GetDlgItem(hwnd, IDC_GENERATE), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_LOAD), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_SAVE), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_SAVEPUB), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH1), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2RSA), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2DSA), 1);
        EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2ECDSA), 1);
        EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2ED25519), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_BITS), 1);
	EnableMenuItem(state->filemenu, IDC_LOAD, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->filemenu, IDC_SAVE, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->filemenu, IDC_SAVEPUB, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_GENERATE, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH1, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH2RSA, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH2DSA, MF_ENABLED|MF_BYCOMMAND);
        EnableMenuItem(state->keymenu, IDC_KEYSSH2ECDSA,
                       MF_ENABLED|MF_BYCOMMAND);
        EnableMenuItem(state->keymenu, IDC_KEYSSH2ED25519,
                       MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_IMPORT, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_EXPORT_OPENSSH_AUTO,
		       MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_EXPORT_OPENSSH_NEW,
		       MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_EXPORT_SSHCOM,
		       MF_GRAYED|MF_BYCOMMAND);
	break;
      case 1:			       /* generating key */
	hidemany(hwnd, nokey_ids, true);
	hidemany(hwnd, generating_ids, false);
	hidemany(hwnd, gotkey_ids, true);
	EnableWindow(GetDlgItem(hwnd, IDC_GENERATE), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_LOAD), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_SAVE), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_SAVEPUB), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH1), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2RSA), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2DSA), 0);
        EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2ECDSA), 0);
        EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2ED25519), 0);
	EnableWindow(GetDlgItem(hwnd, IDC_BITS), 0);
	EnableMenuItem(state->filemenu, IDC_LOAD, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->filemenu, IDC_SAVE, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->filemenu, IDC_SAVEPUB, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_GENERATE, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH1, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH2RSA, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH2DSA, MF_GRAYED|MF_BYCOMMAND);
        EnableMenuItem(state->keymenu, IDC_KEYSSH2ECDSA,
                       MF_GRAYED|MF_BYCOMMAND);
        EnableMenuItem(state->keymenu, IDC_KEYSSH2ED25519,
                       MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_IMPORT, MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_EXPORT_OPENSSH_AUTO,
		       MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_EXPORT_OPENSSH_NEW,
		       MF_GRAYED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_EXPORT_SSHCOM,
		       MF_GRAYED|MF_BYCOMMAND);
	break;
      case 2:
	hidemany(hwnd, nokey_ids, true);
	hidemany(hwnd, generating_ids, true);
	hidemany(hwnd, gotkey_ids, false);
	EnableWindow(GetDlgItem(hwnd, IDC_GENERATE), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_LOAD), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_SAVE), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_SAVEPUB), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH1), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2RSA), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2DSA), 1);
        EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2ECDSA), 1);
        EnableWindow(GetDlgItem(hwnd, IDC_KEYSSH2ED25519), 1);
	EnableWindow(GetDlgItem(hwnd, IDC_BITS), 1);
	EnableMenuItem(state->filemenu, IDC_LOAD, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->filemenu, IDC_SAVE, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->filemenu, IDC_SAVEPUB, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_GENERATE, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH1, MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH2RSA,MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->keymenu, IDC_KEYSSH2DSA,MF_ENABLED|MF_BYCOMMAND);
        EnableMenuItem(state->keymenu, IDC_KEYSSH2ECDSA,
                       MF_ENABLED|MF_BYCOMMAND);
        EnableMenuItem(state->keymenu, IDC_KEYSSH2ED25519,
                       MF_ENABLED|MF_BYCOMMAND);
	EnableMenuItem(state->cvtmenu, IDC_IMPORT, MF_ENABLED|MF_BYCOMMAND);
	/*
	 * Enable export menu items if and only if the key type
	 * supports this kind of export.
	 */
	type = state->ssh2 ? SSH_KEYTYPE_SSH2 : SSH_KEYTYPE_SSH1;
#define do_export_menuitem(x,y) \
    EnableMenuItem(state->cvtmenu, x, MF_BYCOMMAND | \
		       (import_target_type(y)==type?MF_ENABLED:MF_GRAYED))
	do_export_menuitem(IDC_EXPORT_OPENSSH_AUTO, SSH_KEYTYPE_OPENSSH_AUTO);
	do_export_menuitem(IDC_EXPORT_OPENSSH_NEW, SSH_KEYTYPE_OPENSSH_NEW);
	do_export_menuitem(IDC_EXPORT_SSHCOM, SSH_KEYTYPE_SSHCOM);
#undef do_export_menuitem
	break;
    }
}

/*
 * Helper functions to set the key type, taking care of keeping the
 * menu and radio button selections in sync and also showing/hiding
 * the appropriate size/curve control for the current key type.
 */
void ui_update_key_type_ctrls(HWND hwnd)
{
    enum { BITS, CURVE, NOTHING } which;
    static const int bits_ids[] = {
        IDC_BITSSTATIC, IDC_BITS, 0
    };
    static const int curve_ids[] = {
        IDC_CURVESTATIC, IDC_CURVE, 0
    };
    static const int nothing_ids[] = {
        IDC_NOTHINGSTATIC, 0
    };

    if (IsDlgButtonChecked(hwnd, IDC_KEYSSH1) ||
        IsDlgButtonChecked(hwnd, IDC_KEYSSH2RSA) ||
        IsDlgButtonChecked(hwnd, IDC_KEYSSH2DSA)) {
        which = BITS;
    } else if (IsDlgButtonChecked(hwnd, IDC_KEYSSH2ECDSA)) {
        which = CURVE;
    } else {
        /* ED25519 implicitly only supports one curve */
        which = NOTHING;
    }

    hidemany(hwnd, bits_ids, which != BITS);
    hidemany(hwnd, curve_ids, which != CURVE);
    hidemany(hwnd, nothing_ids, which != NOTHING);
}
void ui_set_key_type(HWND hwnd, struct MainDlgState *state, int button)
{
    CheckRadioButton(hwnd, IDC_KEYSSH1, IDC_KEYSSH2ED25519, button);
    CheckMenuRadioItem(state->keymenu, IDC_KEYSSH1, IDC_KEYSSH2ED25519,
                       button, MF_BYCOMMAND);
    ui_update_key_type_ctrls(hwnd);
}

void load_key_file(HWND hwnd, struct MainDlgState *state,
		   Filename *filename, bool was_import_cmd)
{
    char *passphrase;
    bool needs_pass;
    int type, realtype;
    int ret;
    const char *errmsg = NULL;
    char *comment;
    RSAKey newkey1;
    ssh2_userkey *newkey2 = NULL;

    type = realtype = key_type(filename);
    if (type != SSH_KEYTYPE_SSH1 &&
	type != SSH_KEYTYPE_SSH2 &&
	!import_possible(type)) {
	char *msg = dupprintf("Couldn't load private key (%s)",
			      key_type_to_str(type));
	message_box(msg, "PuTTYgen Error", MB_OK | MB_ICONERROR,
		    HELPCTXID(errors_cantloadkey));
	sfree(msg);
	return;
    }

    if (type != SSH_KEYTYPE_SSH1 &&
	type != SSH_KEYTYPE_SSH2) {
	realtype = type;
	type = import_target_type(type);
    }

    comment = NULL;
    passphrase = NULL;
    if (realtype == SSH_KEYTYPE_SSH1)
	needs_pass = rsa_ssh1_encrypted(filename, &comment);
    else if (realtype == SSH_KEYTYPE_SSH2)
	needs_pass = ssh2_userkey_encrypted(filename, &comment);
    else
	needs_pass = import_encrypted(filename, realtype, &comment);
    do {
        burnstr(passphrase);
        passphrase = NULL;

	if (needs_pass) {
	    int dlgret;
            struct PassphraseProcStruct pps;
            pps.passphrase = &passphrase;
            pps.comment = comment;
	    dlgret = DialogBoxParam(hinst,
#ifdef INTEGRATED_KEYGEN
				    MAKEINTRESOURCE(810),
#else
				    MAKEINTRESOURCE(210),
#endif
				    NULL, PassphraseProc,
				    (LPARAM) &pps);
	    if (!dlgret) {
		ret = -2;
		break;
	    }
            assert(passphrase != NULL);
	} else
	    passphrase = dupstr("");
	if (type == SSH_KEYTYPE_SSH1) {
	    if (realtype == type)
		ret = rsa_ssh1_loadkey(
                    filename, &newkey1, passphrase, &errmsg);
	    else
		ret = import_ssh1(filename, realtype, &newkey1,
                                  passphrase, &errmsg);
	} else {
	    if (realtype == type)
		newkey2 = ssh2_load_userkey(filename, passphrase, &errmsg);
	    else
		newkey2 = import_ssh2(filename, realtype, passphrase, &errmsg);
	    if (newkey2 == SSH2_WRONG_PASSPHRASE)
		ret = -1;
	    else if (!newkey2)
		ret = 0;
	    else
		ret = 1;
	}
    } while (ret == -1);
    if (comment)
	sfree(comment);
    if (ret == 0) {
	char *msg = dupprintf("Couldn't load private key (%s)", errmsg);
	message_box(msg, "PuTTYgen Error", MB_OK | MB_ICONERROR,
		    HELPCTXID(errors_cantloadkey));
	sfree(msg);
    } else if (ret == 1) {
	/*
	 * Now update the key controls with all the
	 * key data.
	 */
	{
	    SetDlgItemText(hwnd, IDC_PASSPHRASE1EDIT,
			   passphrase);
	    SetDlgItemText(hwnd, IDC_PASSPHRASE2EDIT,
			   passphrase);
	    if (type == SSH_KEYTYPE_SSH1) {
		char *fingerprint, *savecomment;

		state->ssh2 = false;
		state->commentptr = &state->key.comment;
		state->key = newkey1;

		/*
		 * Set the key fingerprint.
		 */
		savecomment = state->key.comment;
		state->key.comment = NULL;
		fingerprint = rsa_ssh1_fingerprint(&state->key);
		state->key.comment = savecomment;
		SetDlgItemText(hwnd, IDC_FINGERPRINT, fingerprint);
                sfree(fingerprint);

		/*
		 * Construct a decimal representation
		 * of the key, for pasting into
		 * .ssh/authorized_keys on a Unix box.
		 */
		setupbigedit1(hwnd, IDC_KEYDISPLAY,
			      IDC_PKSTATIC, &state->key);
	    } else {
		char *fp;
		char *savecomment;

		state->ssh2 = true;
		state->commentptr =
		    &state->ssh2key.comment;
		state->ssh2key = *newkey2;	/* structure copy */
		sfree(newkey2);

		savecomment = state->ssh2key.comment;
		state->ssh2key.comment = NULL;
		fp = ssh2_fingerprint(state->ssh2key.key);
		state->ssh2key.comment = savecomment;

		SetDlgItemText(hwnd, IDC_FINGERPRINT, fp);
		sfree(fp);

		setupbigedit2(hwnd, IDC_KEYDISPLAY,
			      IDC_PKSTATIC, &state->ssh2key);
	    }
	    SetDlgItemText(hwnd, IDC_COMMENTEDIT,
			   *state->commentptr);
	}
	/*
	 * Finally, hide the progress bar and show
	 * the key data.
	 */
	ui_set_state(hwnd, state, 2);
	state->key_exists = true;

	/*
	 * If the user has imported a foreign key
	 * using the Load command, let them know.
	 * If they've used the Import command, be
	 * silent.
	 */
	if (realtype != type && !was_import_cmd) {
	    char msg[512];
	    sprintf(msg, "Successfully imported foreign key\n"
		    "(%s).\n"
		    "To use this key with PuTTY, you need to\n"
		    "use the \"Save private key\" command to\n"
		    "save it in PuTTY's own format.",
		    key_type_to_str(realtype));
	    MessageBox(NULL, msg, "PuTTYgen Notice",
		       MB_OK | MB_ICONINFORMATION);
	}
    }
    burnstr(passphrase);
}

static void start_generating_key(HWND hwnd, struct MainDlgState *state)
{
    static const char generating_msg[] =
	"Please wait while a key is generated...";

    struct rsa_key_thread_params *params;
    DWORD threadid;

    SetDlgItemText(hwnd, IDC_GENERATING, generating_msg);
    SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETRANGE, 0,
                       MAKELPARAM(0, PROGRESSRANGE));
    SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);

    params = snew(struct rsa_key_thread_params);
    params->progressbar = GetDlgItem(hwnd, IDC_PROGRESS);
    params->dialog = hwnd;
    params->key_bits = state->key_bits;
    params->curve_bits = state->curve_bits;
    params->keytype = state->keytype;
    params->key = &state->key;
    params->dsskey = &state->dsskey;

    if (!CreateThread(NULL, 0, generate_key_thread,
                      params, 0, &threadid)) {
        MessageBox(hwnd, "Out of thread resources",
                   "Key generation error",
                   MB_OK | MB_ICONERROR);
        sfree(params);
    } else {
        state->generation_thread_exists = true;
    }
}

/*
 * Dialog-box function for the main PuTTYgen dialog box.
 */
static INT_PTR CALLBACK MainDlgProc(HWND hwnd, UINT msg,
				WPARAM wParam, LPARAM lParam)
{
    static const char entropy_msg[] =
	"Please generate some randomness by moving the mouse over the blank area.";
    struct MainDlgState *state;

    switch (msg) {
      case WM_INITDIALOG:
        if (has_help())
            SetWindowLongPtr(hwnd, GWL_EXSTYLE,
			     GetWindowLongPtr(hwnd, GWL_EXSTYLE) |
			     WS_EX_CONTEXTHELP);
        else {
            /*
             * If we add a Help button, this is where we destroy it
             * if the help file isn't present.
             */
        }
#ifdef INTEGRATED_KEYGEN
	SendMessage(hwnd, WM_SETICON, (WPARAM) ICON_BIG,
		    (LPARAM) LoadIcon(hinst, MAKEINTRESOURCE(800)));
#else
	SendMessage(hwnd, WM_SETICON, (WPARAM) ICON_BIG,
		    (LPARAM) LoadIcon(hinst, MAKEINTRESOURCE(200)));
#endif

	state = snew(struct MainDlgState);
	state->generation_thread_exists = false;
	state->collecting_entropy = false;
	state->entropy = NULL;
	state->key_exists = false;
	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) state);
	{
	    HMENU menu, menu1;

	    menu = CreateMenu();

	    menu1 = CreateMenu();
	    AppendMenu(menu1, MF_ENABLED, IDC_LOAD, "&Load private key");
	    AppendMenu(menu1, MF_ENABLED, IDC_SAVEPUB, "Save p&ublic key");
	    AppendMenu(menu1, MF_ENABLED, IDC_SAVE, "&Save private key");
	    AppendMenu(menu1, MF_SEPARATOR, 0, 0);
	    AppendMenu(menu1, MF_ENABLED, IDC_QUIT, "E&xit");
	    AppendMenu(menu, MF_POPUP | MF_ENABLED, (UINT_PTR) menu1, "&File");
	    state->filemenu = menu1;

	    menu1 = CreateMenu();
	    AppendMenu(menu1, MF_ENABLED, IDC_GENERATE, "&Generate key pair");
	    AppendMenu(menu1, MF_SEPARATOR, 0, 0);
	    AppendMenu(menu1, MF_ENABLED, IDC_KEYSSH1, "SSH-&1 key (RSA)");
	    AppendMenu(menu1, MF_ENABLED, IDC_KEYSSH2RSA, "SSH-2 &RSA key");
	    AppendMenu(menu1, MF_ENABLED, IDC_KEYSSH2DSA, "SSH-2 &DSA key");
            AppendMenu(menu1, MF_ENABLED, IDC_KEYSSH2ECDSA, "SSH-2 &ECDSA key");
            AppendMenu(menu1, MF_ENABLED, IDC_KEYSSH2ED25519, "SSH-2 Ed&25519 key");
	    AppendMenu(menu, MF_POPUP | MF_ENABLED, (UINT_PTR) menu1, "&Key");
	    state->keymenu = menu1;

	    menu1 = CreateMenu();
	    AppendMenu(menu1, MF_ENABLED, IDC_IMPORT, "&Import key");
	    AppendMenu(menu1, MF_SEPARATOR, 0, 0);
	    AppendMenu(menu1, MF_ENABLED, IDC_EXPORT_OPENSSH_AUTO,
		       "Export &OpenSSH key");
	    AppendMenu(menu1, MF_ENABLED, IDC_EXPORT_OPENSSH_NEW,
		       "Export &OpenSSH key (force new file format)");
	    AppendMenu(menu1, MF_ENABLED, IDC_EXPORT_SSHCOM,
		       "Export &ssh.com key");
	    AppendMenu(menu, MF_POPUP | MF_ENABLED, (UINT_PTR) menu1,
		       "Con&versions");
	    state->cvtmenu = menu1;

	    menu1 = CreateMenu();
	    AppendMenu(menu1, MF_ENABLED, IDC_ABOUT, "&About");
	    if (has_help())
		AppendMenu(menu1, MF_ENABLED, IDC_GIVEHELP, "&Help");
	    AppendMenu(menu, MF_POPUP | MF_ENABLED, (UINT_PTR) menu1, "&Help");

	    SetMenu(hwnd, menu);
	}

	/*
	 * Centre the window.
	 */
	{			       /* centre the window */
	    RECT rs, rd;
	    HWND hw;

	    hw = GetDesktopWindow();
	    if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
		MoveWindow(hwnd,
			   (rs.right + rs.left + rd.left - rd.right) / 2,
			   (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
			   rd.right - rd.left, rd.bottom - rd.top, true);
	}

	{
	    struct ctlpos cp, cp2;
            int ymax;

	    /* Accelerators used: acglops1rbvde */

	    ctlposinit(&cp, hwnd, 4, 4, 4);
	    beginbox(&cp, "Key", IDC_BOX_KEY);
	    cp2 = cp;
	    statictext(&cp2, "No key.", 1, IDC_NOKEY);
	    cp2 = cp;
	    statictext(&cp2, "", 1, IDC_GENERATING);
	    progressbar(&cp2, IDC_PROGRESS);
	    bigeditctrl(&cp,
			"&Public key for pasting into authorized_keys file:",
			IDC_PKSTATIC, IDC_KEYDISPLAY, 5);
	    SendDlgItemMessage(hwnd, IDC_KEYDISPLAY, EM_SETREADONLY, 1, 0);
	    staticedit(&cp, "Key f&ingerprint:", IDC_FPSTATIC,
		       IDC_FINGERPRINT, 75);
	    SendDlgItemMessage(hwnd, IDC_FINGERPRINT, EM_SETREADONLY, 1,
			       0);
	    staticedit(&cp, "Key &comment:", IDC_COMMENTSTATIC,
		       IDC_COMMENTEDIT, 75);
	    staticpassedit(&cp, "Key p&assphrase:", IDC_PASSPHRASE1STATIC,
			   IDC_PASSPHRASE1EDIT, 75);
	    staticpassedit(&cp, "C&onfirm passphrase:",
			   IDC_PASSPHRASE2STATIC, IDC_PASSPHRASE2EDIT, 75);
	    endbox(&cp);
	    beginbox(&cp, "Actions", IDC_BOX_ACTIONS);
	    staticbtn(&cp, "Generate a public/private key pair",
		      IDC_GENSTATIC, "&Generate", IDC_GENERATE);
	    staticbtn(&cp, "Load an existing private key file",
		      IDC_LOADSTATIC, "&Load", IDC_LOAD);
	    static2btn(&cp, "Save the generated key", IDC_SAVESTATIC,
		       "Save p&ublic key", IDC_SAVEPUB,
		       "&Save private key", IDC_SAVE);
	    endbox(&cp);
	    beginbox(&cp, "Parameters", IDC_BOX_PARAMS);
	    radioline(&cp, "Type of key to generate:", IDC_TYPESTATIC, 5,
		      "&RSA", IDC_KEYSSH2RSA,
                      "&DSA", IDC_KEYSSH2DSA,
                      "&ECDSA", IDC_KEYSSH2ECDSA,
                      "Ed&25519", IDC_KEYSSH2ED25519,
		      "SSH-&1 (RSA)", IDC_KEYSSH1,
                      NULL);
            cp2 = cp;
	    staticedit(&cp2, "Number of &bits in a generated key:",
		       IDC_BITSSTATIC, IDC_BITS, 20);
            ymax = cp2.ypos;
            cp2 = cp;
	    staticddl(&cp2, "Cur&ve to use for generating this key:",
                      IDC_CURVESTATIC, IDC_CURVE, 20);
            SendDlgItemMessage(hwnd, IDC_CURVE, CB_RESETCONTENT, 0, 0);
            {
                int i, bits;
                const struct ec_curve *curve;
                const ssh_keyalg *alg;

                for (i = 0; i < n_ec_nist_curve_lengths; i++) {
                    bits = ec_nist_curve_lengths[i];
                    ec_nist_alg_and_curve_by_bits(bits, &curve, &alg);
                    SendDlgItemMessage(hwnd, IDC_CURVE, CB_ADDSTRING, 0,
                                       (LPARAM)curve->textname);
                }
            }
            ymax = ymax > cp2.ypos ? ymax : cp2.ypos;
            cp2 = cp;
	    statictext(&cp2, "(nothing to configure for this key type)",
		       1, IDC_NOTHINGSTATIC);
            ymax = ymax > cp2.ypos ? ymax : cp2.ypos;
            cp.ypos = ymax;
	    endbox(&cp);
	}
        ui_set_key_type(hwnd, state, IDC_KEYSSH2RSA);
	SetDlgItemInt(hwnd, IDC_BITS, DEFAULT_KEY_BITS, false);
	SendDlgItemMessage(hwnd, IDC_CURVE, CB_SETCURSEL,
                           DEFAULT_CURVE_INDEX, 0);

	/*
	 * Initially, hide the progress bar and the key display,
	 * and show the no-key display. Also disable the Save
	 * buttons, because with no key we obviously can't save
	 * anything.
	 */
	ui_set_state(hwnd, state, 0);

	/*
	 * Load a key file if one was provided on the command line.
	 */
	if (cmdline_keyfile) {
            Filename *fn = filename_from_str(cmdline_keyfile);
	    load_key_file(hwnd, state, fn, false);
            filename_free(fn);
        }

	return 1;
      case WM_MOUSEMOVE:
	state = (struct MainDlgState *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (state->collecting_entropy &&
	    state->entropy && state->entropy_got < state->entropy_required) {
	    state->entropy[state->entropy_got++] = lParam;
	    state->entropy[state->entropy_got++] = GetMessageTime();
	    SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS,
			       state->entropy_got, 0);
	    if (state->entropy_got >= state->entropy_required) {
		/*
		 * Seed the entropy pool
		 */
                random_reseed(
                    make_ptrlen(state->entropy, state->entropy_size));
		smemclr(state->entropy, state->entropy_size);
		sfree(state->entropy);
		state->collecting_entropy = false;

                start_generating_key(hwnd, state);
	    }
	}
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDC_KEYSSH1:
	  case IDC_KEYSSH2RSA:
	  case IDC_KEYSSH2DSA:
          case IDC_KEYSSH2ECDSA:
          case IDC_KEYSSH2ED25519:
	    {
		state = (struct MainDlgState *)
		    GetWindowLongPtr(hwnd, GWLP_USERDATA);
                ui_set_key_type(hwnd, state, LOWORD(wParam));
	    }
	    break;
	  case IDC_QUIT:
	    PostMessage(hwnd, WM_CLOSE, 0, 0);
	    break;
	  case IDC_COMMENTEDIT:
	    if (HIWORD(wParam) == EN_CHANGE) {
		state = (struct MainDlgState *)
		    GetWindowLongPtr(hwnd, GWLP_USERDATA);
		if (state->key_exists) {
		    HWND editctl = GetDlgItem(hwnd, IDC_COMMENTEDIT);
		    int len = GetWindowTextLength(editctl);
		    if (*state->commentptr)
			sfree(*state->commentptr);
		    *state->commentptr = snewn(len + 1, char);
		    GetWindowText(editctl, *state->commentptr, len + 1);
		    if (state->ssh2) {
			setupbigedit2(hwnd, IDC_KEYDISPLAY, IDC_PKSTATIC,
				      &state->ssh2key);
		    } else {
			setupbigedit1(hwnd, IDC_KEYDISPLAY, IDC_PKSTATIC,
				      &state->key);
		    }
		}
	    }
	    break;
	  case IDC_ABOUT:
	    EnableWindow(hwnd, 0);
#ifdef INTEGRATED_KEYGEN
	    DialogBox(hinst, MAKEINTRESOURCE(813), hwnd, AboutProc);
#else
	    DialogBox(hinst, MAKEINTRESOURCE(213), hwnd, AboutProc);
#endif
	    EnableWindow(hwnd, 1);
	    SetActiveWindow(hwnd);
	    return 0;
	  case IDC_GIVEHELP:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED) {
		launch_help(hwnd, WINHELP_CTX_puttygen_general);
            }
	    return 0;
	  case IDC_GENERATE:
            if (HIWORD(wParam) != BN_CLICKED &&
                HIWORD(wParam) != BN_DOUBLECLICKED)
		break;
	    state =
		(struct MainDlgState *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	    if (!state->generation_thread_exists) {
                unsigned raw_entropy_required;
                unsigned char *raw_entropy_buf;
		BOOL ok;
		state->key_bits = GetDlgItemInt(hwnd, IDC_BITS, &ok, false);
		if (!ok)
		    state->key_bits = DEFAULT_KEY_BITS;
                {
                    int curveindex = SendDlgItemMessage(hwnd, IDC_CURVE,
                                                        CB_GETCURSEL, 0, 0);
                    assert(curveindex >= 0);
                    assert(curveindex < n_ec_nist_curve_lengths);
                    state->curve_bits = ec_nist_curve_lengths[curveindex];
                }
		/* If we ever introduce a new key type, check it here! */
		state->ssh2 = !IsDlgButtonChecked(hwnd, IDC_KEYSSH1);
                state->keytype = RSA;
                if (IsDlgButtonChecked(hwnd, IDC_KEYSSH2DSA)) {
                    state->keytype = DSA;
                } else if (IsDlgButtonChecked(hwnd, IDC_KEYSSH2ECDSA)) {
                    state->keytype = ECDSA;
                } else if (IsDlgButtonChecked(hwnd, IDC_KEYSSH2ED25519)) {
                    state->keytype = ED25519;
                }

		if ((state->keytype == RSA || state->keytype == DSA) &&
                    state->key_bits < 256) {
                    char *message = dupprintf
                        ("PuTTYgen will not generate a key smaller than 256"
                         " bits.\nKey length reset to default %d. Continue?",
                         DEFAULT_KEY_BITS);
		    int ret = MessageBox(hwnd, message, "PuTTYgen Warning",
					 MB_ICONWARNING | MB_OKCANCEL);
                    sfree(message);
		    if (ret != IDOK)
			break;
		    state->key_bits = DEFAULT_KEY_BITS;
		    SetDlgItemInt(hwnd, IDC_BITS, DEFAULT_KEY_BITS, false);
		} else if ((state->keytype == RSA || state->keytype == DSA) &&
                           state->key_bits < DEFAULT_KEY_BITS) {
                    char *message = dupprintf
                        ("Keys shorter than %d bits are not recommended. "
                         "Really generate this key?", DEFAULT_KEY_BITS);
		    int ret = MessageBox(hwnd, message, "PuTTYgen Warning",
					 MB_ICONWARNING | MB_OKCANCEL);
                    sfree(message);
		    if (ret != IDOK)
			break;
                }

		if (state->keytype == RSA || state->keytype == DSA)
                    raw_entropy_required = (state->key_bits / 2) * 2;
		else if (state->keytype == ECDSA)
                    raw_entropy_required = (state->curve_bits / 2) * 2;
                else
                    raw_entropy_required = 256;

                /* Bound the entropy collection above by the amount of
                 * data we can actually fit into the PRNG. Any more
                 * than that and it's doing no more good. */
                if (raw_entropy_required > random_seed_bits())
                    raw_entropy_required = random_seed_bits();

                raw_entropy_buf = snewn(raw_entropy_required, unsigned char);
                if (win_read_random(raw_entropy_buf, raw_entropy_required)) {
                    /*
                     * If we can get entropy from CryptGenRandom, use
                     * it. But CryptGenRandom isn't a kernel-level
                     * CPRNG (according to Wikipedia), and papers have
                     * been published cryptanalysing it. So we'll
                     * still do manual entropy collection; we'll just
                     * do it _as well_ as this.
                     */
                    random_reseed(
                        make_ptrlen(raw_entropy_buf, raw_entropy_required));
                }

                /*
                 * Manual entropy input, by making the user wave the
                 * mouse over the window a lot.
                 *
                 * My brief statistical tests on mouse movements
                 * suggest that there are about 2.5 bits of randomness
                 * in the x position, 2.5 in the y position, and 1.7
                 * in the message time, making 5.7 bits of
                 * unpredictability per mouse movement. However, other
                 * people have told me it's far less than that, so I'm
                 * going to be stupidly cautious and knock that down
                 * to a nice round 2. With this method, we require two
                 * words per mouse movement, so with 2 bits per mouse
                 * movement we expect 2 bits every 2 words, i.e. the
                 * number of _words_ of mouse data we want to collect
                 * is just the same as the number of _bits_ of entropy
                 * we want.
                 */
                state->entropy_required = raw_entropy_required;

                ui_set_state(hwnd, state, 1);
                SetDlgItemText(hwnd, IDC_GENERATING, entropy_msg);
                state->key_exists = false;
                state->collecting_entropy = true;

                state->entropy_got = 0;
                state->entropy_size = (state->entropy_required *
                                       sizeof(unsigned));
                state->entropy = snewn(state->entropy_required, unsigned);

                SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETRANGE, 0,
                                   MAKELPARAM(0, state->entropy_required));
                SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS, 0, 0);

                smemclr(raw_entropy_buf, raw_entropy_required);
                sfree(raw_entropy_buf);
	    }
	    break;
	  case IDC_SAVE:
          case IDC_EXPORT_OPENSSH_AUTO:
          case IDC_EXPORT_OPENSSH_NEW:
          case IDC_EXPORT_SSHCOM:
	    if (HIWORD(wParam) != BN_CLICKED)
		break;
	    state =
		(struct MainDlgState *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	    if (state->key_exists) {
		char filename[FILENAME_MAX];
		char *passphrase, *passphrase2;
                int type, realtype;

                if (state->ssh2)
                    realtype = SSH_KEYTYPE_SSH2;
                else
                    realtype = SSH_KEYTYPE_SSH1;

                if (LOWORD(wParam) == IDC_EXPORT_OPENSSH_AUTO)
                    type = SSH_KEYTYPE_OPENSSH_AUTO;
                else if (LOWORD(wParam) == IDC_EXPORT_OPENSSH_NEW)
                    type = SSH_KEYTYPE_OPENSSH_NEW;
                else if (LOWORD(wParam) == IDC_EXPORT_SSHCOM)
                    type = SSH_KEYTYPE_SSHCOM;
                else
                    type = realtype;

                if (type != realtype &&
                    import_target_type(type) != realtype) {
                    char msg[256];
                    sprintf(msg, "Cannot export an SSH-%d key in an SSH-%d"
                            " format", (state->ssh2 ? 2 : 1),
                            (state->ssh2 ? 1 : 2));
		    MessageBox(hwnd, msg,
                               "PuTTYgen Error", MB_OK | MB_ICONERROR);
		    break;
                }

		passphrase = GetDlgItemText_alloc(hwnd, IDC_PASSPHRASE1EDIT);
		passphrase2 = GetDlgItemText_alloc(hwnd, IDC_PASSPHRASE2EDIT);
		if (strcmp(passphrase, passphrase2)) {
		    MessageBox(hwnd,
			       "The two passphrases given do not match.",
			       "PuTTYgen Error", MB_OK | MB_ICONERROR);
                    burnstr(passphrase);
                    burnstr(passphrase2);
		    break;
		}
                burnstr(passphrase2);
		if (!*passphrase) {
		    int ret;
		    ret = MessageBox(hwnd,
				     "Are you sure you want to save this key\n"
				     "without a passphrase to protect it?",
				     "PuTTYgen Warning",
				     MB_YESNO | MB_ICONWARNING);
		    if (ret != IDYES) {
                        burnstr(passphrase);
                        break;
                    }
		}
		if (prompt_keyfile(hwnd, "Save private key as:",
				   filename, true, (type == realtype))) {
		    int ret;
		    FILE *fp = fopen(filename, "r");
		    if (fp) {
			char *buffer;
			fclose(fp);
			buffer = dupprintf("Overwrite existing file\n%s?",
					   filename);
			ret = MessageBox(hwnd, buffer, "PuTTYgen Warning",
					 MB_YESNO | MB_ICONWARNING);
			sfree(buffer);
			if (ret != IDYES) {
                            burnstr(passphrase);
			    break;
                        }
		    }

		    if (state->ssh2) {
			Filename *fn = filename_from_str(filename);
                        if (type != realtype)
                            ret = export_ssh2(fn, type, &state->ssh2key,
                                              *passphrase ? passphrase : NULL);
                        else
                            ret = ssh2_save_userkey(fn, &state->ssh2key,
                                                    *passphrase ? passphrase :
                                                    NULL);
                        filename_free(fn);
		    } else {
			Filename *fn = filename_from_str(filename);
                        if (type != realtype)
                            ret = export_ssh1(fn, type, &state->key,
                                              *passphrase ? passphrase : NULL);
                        else
                            ret = rsa_ssh1_savekey(
                                fn, &state->key,
                                *passphrase ? passphrase : NULL);
                        filename_free(fn);
		    }
		    if (ret <= 0) {
			MessageBox(hwnd, "Unable to save key file",
				   "PuTTYgen Error", MB_OK | MB_ICONERROR);
		    }
		}
                burnstr(passphrase);
	    }
	    break;
	  case IDC_SAVEPUB:
	    if (HIWORD(wParam) != BN_CLICKED)
		break;
	    state =
		(struct MainDlgState *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	    if (state->key_exists) {
		char filename[FILENAME_MAX];
		if (prompt_keyfile(hwnd, "Save public key as:",
				   filename, true, false)) {
		    int ret;
		    FILE *fp = fopen(filename, "r");
		    if (fp) {
			char *buffer;
			fclose(fp);
			buffer = dupprintf("Overwrite existing file\n%s?",
					   filename);
			ret = MessageBox(hwnd, buffer, "PuTTYgen Warning",
					 MB_YESNO | MB_ICONWARNING);
			sfree(buffer);
			if (ret != IDYES)
			    break;
		    }
                    fp = fopen(filename, "w");
                    if (!fp) {
                        MessageBox(hwnd, "Unable to open key file",
                                   "PuTTYgen Error", MB_OK | MB_ICONERROR);
                    } else {
                        if (state->ssh2) {
                            strbuf *blob = strbuf_new();
                            ssh_key_public_blob(
                                state->ssh2key.key, BinarySink_UPCAST(blob));
                            ssh2_write_pubkey(fp, state->ssh2key.comment,
                                              blob->u, blob->len,
                                              SSH_KEYTYPE_SSH2_PUBLIC_RFC4716);
                            strbuf_free(blob);
                        } else {
                            ssh1_write_pubkey(fp, &state->key);
                        }
                        if (fclose(fp) < 0) {
                            MessageBox(hwnd, "Unable to save key file",
                                       "PuTTYgen Error", MB_OK | MB_ICONERROR);
                        }
                    }
		}
	    }
	    break;
	  case IDC_LOAD:
	  case IDC_IMPORT:
	    if (HIWORD(wParam) != BN_CLICKED)
		break;
	    state =
		(struct MainDlgState *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	    if (!state->generation_thread_exists) {
		char filename[FILENAME_MAX];
		if (prompt_keyfile(hwnd, "Load private key:", filename, false,
                                   LOWORD(wParam) == IDC_LOAD)) {
                    Filename *fn = filename_from_str(filename);
		    load_key_file(hwnd, state, fn, LOWORD(wParam) != IDC_LOAD);
                    filename_free(fn);
                }
	    }
	    break;
	}
	return 0;
      case WM_DONEKEY:
	state = (struct MainDlgState *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	state->generation_thread_exists = false;
	state->key_exists = true;
	SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETRANGE, 0,
			   MAKELPARAM(0, PROGRESSRANGE));
	SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETPOS, PROGRESSRANGE, 0);
	if (state->ssh2) {
            if (state->keytype == DSA) {
		state->ssh2key.key = &state->dsskey.sshk;
            } else if (state->keytype == ECDSA) {
                state->ssh2key.key = &state->eckey.sshk;
            } else if (state->keytype == ED25519) {
                state->ssh2key.key = &state->edkey.sshk;
	    } else {
		state->ssh2key.key = &state->key.sshk;
	    }
	    state->commentptr = &state->ssh2key.comment;
	} else {
	    state->commentptr = &state->key.comment;
	}
	/*
	 * Invent a comment for the key. We'll do this by including
	 * the date in it. This will be so horrifyingly ugly that
	 * the user will immediately want to change it, which is
	 * what we want :-)
	 */
	*state->commentptr = snewn(30, char);
	{
	    struct tm tm;
	    tm = ltime();
            if (state->keytype == DSA)
		strftime(*state->commentptr, 30, "dsa-key-%Y%m%d", &tm);
            else if (state->keytype == ECDSA)
                strftime(*state->commentptr, 30, "ecdsa-key-%Y%m%d", &tm);
            else if (state->keytype == ED25519)
                strftime(*state->commentptr, 30, "ed25519-key-%Y%m%d", &tm);
	    else
		strftime(*state->commentptr, 30, "rsa-key-%Y%m%d", &tm);
	}

	/*
	 * Now update the key controls with all the key data.
	 */
	{
	    char *fp, *savecomment;
	    /*
	     * Blank passphrase, initially. This isn't dangerous,
	     * because we will warn (Are You Sure?) before allowing
	     * the user to save an unprotected private key.
	     */
	    SetDlgItemText(hwnd, IDC_PASSPHRASE1EDIT, "");
	    SetDlgItemText(hwnd, IDC_PASSPHRASE2EDIT, "");
	    /*
	     * Set the comment.
	     */
	    SetDlgItemText(hwnd, IDC_COMMENTEDIT, *state->commentptr);
	    /*
	     * Set the key fingerprint.
	     */
	    savecomment = *state->commentptr;
	    *state->commentptr = NULL;
	    if (state->ssh2)
		fp = ssh2_fingerprint(state->ssh2key.key);
            else
                fp = rsa_ssh1_fingerprint(&state->key);
            SetDlgItemText(hwnd, IDC_FINGERPRINT, fp);
            sfree(fp);
	    *state->commentptr = savecomment;
	    /*
	     * Construct a decimal representation of the key, for
	     * pasting into .ssh/authorized_keys or
	     * .ssh/authorized_keys2 on a Unix box.
	     */
	    if (state->ssh2) {
		setupbigedit2(hwnd, IDC_KEYDISPLAY,
			      IDC_PKSTATIC, &state->ssh2key);
	    } else {
		setupbigedit1(hwnd, IDC_KEYDISPLAY,
			      IDC_PKSTATIC, &state->key);
	    }
	}
	/*
	 * Finally, hide the progress bar and show the key data.
	 */
	ui_set_state(hwnd, state, 2);
	break;
      case WM_HELP:
        {
            int id = ((LPHELPINFO)lParam)->iCtrlId;
            const char *topic = NULL;
            switch (id) {
              case IDC_GENERATING:
              case IDC_PROGRESS:
              case IDC_GENSTATIC:
              case IDC_GENERATE:
                topic = WINHELP_CTX_puttygen_generate; break;
              case IDC_PKSTATIC:
              case IDC_KEYDISPLAY:
                topic = WINHELP_CTX_puttygen_pastekey; break;
              case IDC_FPSTATIC:
              case IDC_FINGERPRINT:
                topic = WINHELP_CTX_puttygen_fingerprint; break;
              case IDC_COMMENTSTATIC:
              case IDC_COMMENTEDIT:
                topic = WINHELP_CTX_puttygen_comment; break;
              case IDC_PASSPHRASE1STATIC:
              case IDC_PASSPHRASE1EDIT:
              case IDC_PASSPHRASE2STATIC:
              case IDC_PASSPHRASE2EDIT:
                topic = WINHELP_CTX_puttygen_passphrase; break;
              case IDC_LOADSTATIC:
              case IDC_LOAD:
                topic = WINHELP_CTX_puttygen_load; break;
              case IDC_SAVESTATIC:
              case IDC_SAVE:
                topic = WINHELP_CTX_puttygen_savepriv; break;
              case IDC_SAVEPUB:
                topic = WINHELP_CTX_puttygen_savepub; break;
              case IDC_TYPESTATIC:
              case IDC_KEYSSH1:
              case IDC_KEYSSH2RSA:
              case IDC_KEYSSH2DSA:
              case IDC_KEYSSH2ECDSA:
              case IDC_KEYSSH2ED25519:
                topic = WINHELP_CTX_puttygen_keytype; break;
              case IDC_BITSSTATIC:
              case IDC_BITS:
                topic = WINHELP_CTX_puttygen_bits; break;
              case IDC_IMPORT:
              case IDC_EXPORT_OPENSSH_AUTO:
              case IDC_EXPORT_OPENSSH_NEW:
              case IDC_EXPORT_SSHCOM:
                topic = WINHELP_CTX_puttygen_conversions; break;
            }
            if (topic) {
                launch_help(hwnd, topic);
            } else {
                MessageBeep(0);
            }
        }
        break;
      case WM_CLOSE:
	state = (struct MainDlgState *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	sfree(state);
	quit_help(hwnd);
	EndDialog(hwnd, 1);
	return 0;
    }
    return 0;
}

#ifndef INTEGRATED_KEYGEN
void cleanup_exit(int code)
{
    shutdown_help();
    exit(code);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
#else
int WINAPI KeyGen_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
#endif
{
    int argc, i;
    char **argv;
    int ret;

    dll_hijacking_protection();

    init_common_controls();
    hinst = inst;
    hwnd = NULL;

    /*
     * See if we can find our Help file.
     */
    init_help();

    split_into_argv(cmdline, &argc, &argv, NULL);

    for (i = 0; i < argc; i++) {
	if (!strcmp(argv[i], "-pgpfp")) {
	    pgp_fingerprints();
	    return 1;
        } else if (!strcmp(argv[i], "-restrict-acl") ||
                   !strcmp(argv[i], "-restrict_acl") ||
                   !strcmp(argv[i], "-restrictacl")) {
            restrict_process_acl();
	} else {
	    /*
	     * Assume the first argument to be a private key file, and
	     * attempt to load it.
	     */
	    cmdline_keyfile = argv[i];
            break;
	}
    }

    random_setup_special();
    ret = DialogBox(hinst, MAKEINTRESOURCE(201), NULL, MainDlgProc) != IDOK;

    cleanup_exit(ret);
    return ret;			       /* just in case optimiser complains */
}
