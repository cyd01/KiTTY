/*
 * Pageant: the PuTTY Authentication Agent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <assert.h>
#include <tchar.h>

#define PUTTY_DO_GLOBALS

#include "putty.h"
#include "ssh.h"
#include "misc.h"
#include "tree234.h"
#include "winsecur.h"
#include "pageant.h"
#include "licence.h"

#include <shellapi.h>

#ifdef MOD_PERSO
void scrumble_int();
// Flag pour le fonctionnement en mode "portable" (gestion par fichiers)
extern int IniFileFlag ;

// Flag permettant la gestion de l'arborscence (dossier=folder) dans le cas d'un savemode=dir
extern int DirectoryBrowseFlag ;

#include "../../kitty_crypt.h"
#include "../../kitty_commun.h"
#include "../../kitty_store.h"
#include "../storage.h"

static char pphrase[2048]="";
#endif

#ifndef NO_SECURITY
#include <aclapi.h>
#ifdef DEBUG_IPC
#define _WIN32_WINNT 0x0500            /* for ConvertSidToStringSid */
#include <sddl.h>
#endif
#endif

#ifndef MOD_INTEGRATED_AGENT
#define IDI_MAINICON 200
#define IDI_TRAYICON 201
#else
#define IDI_MAINICON 900
#define IDI_TRAYICON 901
#endif

#define WM_SYSTRAY   (WM_APP + 6)
#define WM_SYSTRAY2  (WM_APP + 7)

#define AGENT_COPYDATA_ID 0x804e50ba   /* random goop */

/* From MSDN: In the WM_SYSCOMMAND message, the four low-order bits of
 * wParam are used by Windows, and should be masked off, so we shouldn't
 * attempt to store information in them. Hence all these identifiers have
 * the low 4 bits clear. Also, identifiers should < 0xF000. */

#define IDM_CLOSE    0x0010
#define IDM_VIEWKEYS 0x0020
#define IDM_ADDKEY   0x0030
#define IDM_HELP     0x0040
#define IDM_ABOUT    0x0050

#ifdef MOD_PERSO
#define APPNAME "Pageant"
#endif

static HWND keylist;
static HWND aboutbox;
static HMENU systray_menu, session_menu;
static bool already_running;

static char *putty_path;
static bool restrict_putty_acl = false;

/* CWD for "add key" file requester. */
static filereq *keypath = NULL;

#define IDM_PUTTY         0x0060
#define IDM_SESSIONS_BASE 0x1000
#define IDM_SESSIONS_MAX  0x2000
#ifdef MOD_PERSO
#define PUTTY_REGKEY      "Software\\9bis.com\\KiTTY\\Sessions"
#else
#define PUTTY_REGKEY      "Software\\SimonTatham\\PuTTY\\Sessions"
#endif

#define PUTTY_DEFAULT     "Default%20Settings"
static int initial_menuitems_count;

#ifndef MOD_INTEGRATED_AGENT
/*
 * Print a modal (Really Bad) message box and perform a fatal exit.
 */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char *buf;

    va_start(ap, fmt);
    buf = dupvprintf(fmt, ap);
    va_end(ap);
    MessageBox(hwnd, buf, "Pageant Fatal Error",
	       MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(buf);
    exit(1);
}
#endif

static bool has_security;

struct PassphraseProcStruct {
    char **passphrase;
    char *comment;
};

/*
 * Dialog-box function for the Licence box.
 */
static INT_PTR CALLBACK LicenceProc(HWND hwnd, UINT msg,
				WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG:
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
        {
            char *buildinfo_text = buildinfo("\r\n");
            char *text = dupprintf
                ("Pageant\r\n\r\n%s\r\n\r\n%s\r\n\r\n%s",
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
	    aboutbox = NULL;
	    DestroyWindow(hwnd);
	    return 0;
	  case 101:
	    EnableWindow(hwnd, 0);
	    DialogBox(hinst, MAKEINTRESOURCE(214), hwnd, LicenceProc);
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
	aboutbox = NULL;
	DestroyWindow(hwnd);
	return 0;
    }
    return 0;
}

static HWND passphrase_box;

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
	passphrase_box = hwnd;
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

	SetForegroundWindow(hwnd);
	SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
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

#ifndef MOD_INTEGRATED_AGENT
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
	"You can perform this conversion by loading the key\n"
	"into PuTTYgen and then saving it again.";

    MessageBox(NULL, message, mbtitle, MB_OK);
}
#endif

/*
 * Update the visible key list.
 */
void keylist_update(void)
{
    RSAKey *rkey;
    ssh2_userkey *skey;
    int i;

    if (keylist) {
	SendDlgItemMessage(keylist, 100, LB_RESETCONTENT, 0, 0);
	for (i = 0; NULL != (rkey = pageant_nth_ssh1_key(i)); i++) {
	    char *listentry, *fp, *p;

	    fp = rsa_ssh1_fingerprint(rkey);
	    listentry = dupprintf("ssh1\t%s", fp);
            sfree(fp);

	    /*
	     * Replace two spaces in the fingerprint with tabs, for
	     * nice alignment in the box.
	     */
	    p = strchr(listentry, ' ');
	    if (p)
		*p = '\t';
	    p = strchr(listentry, ' ');
	    if (p)
		*p = '\t';
	    SendDlgItemMessage(keylist, 100, LB_ADDSTRING,
			       0, (LPARAM) listentry);
            sfree(listentry);
	}
	for (i = 0; NULL != (skey = pageant_nth_ssh2_key(i)); i++) {
	    char *listentry, *p;
	    int pos;

            /*
             * For nice alignment in the list box, we would ideally
             * want every entry to align to the tab stop settings, and
             * have a column for algorithm name, one for bit count,
             * one for hex fingerprint, and one for key comment.
             *
             * Unfortunately, some of the algorithm names are so long
             * that they overflow into the bit-count field.
             * Fortunately, at the moment, those are _precisely_ the
             * algorithm names that don't need a bit count displayed
             * anyway (because for NIST-style ECDSA the bit count is
             * mentioned in the algorithm name, and for ssh-ed25519
             * there is only one possible value anyway). So we fudge
             * this by simply omitting the bit count field in that
             * situation.
             *
             * This is fragile not only in the face of further key
             * types that don't follow this pattern, but also in the
             * face of font metrics changes - the Windows semantics
             * for list box tab stops is that \t aligns to the next
             * one you haven't already exceeded, so I have to guess
             * when the key type will overflow past the bit-count tab
             * stop and leave out a tab character. Urgh.
             */

	    p = ssh2_fingerprint(skey->key);
            listentry = dupprintf("%s\t%s", p, skey->comment);
            sfree(p);

            pos = 0;
            while (1) {
                pos += strcspn(listentry + pos, " :");
                if (listentry[pos] == ':' || !listentry[pos])
                    break;
                listentry[pos++] = '\t';
            }
            if (ssh_key_alg(skey->key) != &ssh_dss &&
                ssh_key_alg(skey->key) != &ssh_rsa) {
                /*
                 * Remove the bit-count field, which is between the
                 * first and second \t.
                 */
                int outpos;
                pos = 0;
                while (listentry[pos] && listentry[pos] != '\t')
                    pos++;
                outpos = pos;
                pos++;
                while (listentry[pos] && listentry[pos] != '\t')
                    pos++;
                while (1) {
                    if ((listentry[outpos] = listentry[pos]) == '\0')
                        break;
                    outpos++;
                    pos++;
                }
            }

	    SendDlgItemMessage(keylist, 100, LB_ADDSTRING, 0,
			       (LPARAM) listentry);
            sfree(listentry);
	}
	SendDlgItemMessage(keylist, 100, LB_SETCURSEL, (WPARAM) - 1, 0);
    }
}

static void win_add_keyfile(Filename *filename)
{
    char *err;
    int ret;
    char *passphrase = NULL;

    /*
     * Try loading the key without a passphrase. (Or rather, without a
     * _new_ passphrase; pageant_add_keyfile will take care of trying
     * all the passphrases we've already stored.)
     */
    ret = pageant_add_keyfile(filename, NULL, &err);
    if (ret == PAGEANT_ACTION_OK) {
        goto done;
    } else if (ret == PAGEANT_ACTION_FAILURE) {
        goto error;
    }

    /*
     * OK, a passphrase is needed, and we've been given the key
     * comment to use in the passphrase prompt.
     */
    while (1) {
        INT_PTR dlgret;
        struct PassphraseProcStruct pps;

        pps.passphrase = &passphrase;
        pps.comment = err;
#ifdef MOD_PERSO
	if( strlen(pphrase)>0 ) {
	    passphrase=(char*)malloc(strlen(pphrase)+1);
	    strcpy(passphrase,pphrase);
	    
        } 
	else
#endif
        dlgret = DialogBoxParam(hinst, MAKEINTRESOURCE(210),
                                NULL, PassphraseProc, (LPARAM) &pps);
        passphrase_box = NULL;

        if (!dlgret)
            goto done;		       /* operation cancelled */

        sfree(err);

        assert(passphrase != NULL);

        ret = pageant_add_keyfile(filename, passphrase, &err);
        if (ret == PAGEANT_ACTION_OK) {
            goto done;
        } else if (ret == PAGEANT_ACTION_FAILURE) {
            goto error;
        }

        smemclr(passphrase, strlen(passphrase));
        sfree(passphrase);
        passphrase = NULL;
    }

  error:
    message_box(err, APPNAME, MB_OK | MB_ICONERROR,
                HELPCTXID(errors_cantloadkey));
  done:
    if (passphrase) {
        smemclr(passphrase, strlen(passphrase));
        sfree(passphrase);
    }
    sfree(err);
    return;
}

/*
 * Prompt for a key file to add, and add it.
 */
static void prompt_add_keyfile(void)
{
    OPENFILENAME of;
    char *filelist = snewn(8192, char);
	
    if (!keypath) keypath = filereq_new();
    memset(&of, 0, sizeof(of));
    of.hwndOwner = hwnd;
    of.lpstrFilter = FILTER_KEY_FILES;
    of.lpstrCustomFilter = NULL;
    of.nFilterIndex = 1;
    of.lpstrFile = filelist;
    *filelist = '\0';
    of.nMaxFile = 8192;
    of.lpstrFileTitle = NULL;
    of.lpstrTitle = "Select Private Key File";
    of.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (request_file(keypath, &of, true, false)) {
	if(strlen(filelist) > of.nFileOffset) {
	    /* Only one filename returned? */
            Filename *fn = filename_from_str(filelist);
	    win_add_keyfile(fn);
            filename_free(fn);
        } else {
	    /* we are returned a bunch of strings, end to
	     * end. first string is the directory, the
	     * rest the filenames. terminated with an
	     * empty string.
	     */
	    char *dir = filelist;
	    char *filewalker = filelist + strlen(dir) + 1;
	    while (*filewalker != '\0') {
                char *filename = dupcat(dir, "\\", filewalker);
                Filename *fn = filename_from_str(filename);
		win_add_keyfile(fn);
                filename_free(fn);
		sfree(filename);
		filewalker += strlen(filewalker) + 1;
	    }
	}

	keylist_update();
	pageant_forget_passphrases();
    }
    sfree(filelist);
}

/*
 * Dialog-box function for the key list box.
 */
static INT_PTR CALLBACK KeyListProc(HWND hwnd, UINT msg,
				WPARAM wParam, LPARAM lParam)
{
    RSAKey *rkey;
    ssh2_userkey *skey;

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

        if (has_help())
            SetWindowLongPtr(hwnd, GWL_EXSTYLE,
			     GetWindowLongPtr(hwnd, GWL_EXSTYLE) |
			     WS_EX_CONTEXTHELP);
        else {
            HWND item = GetDlgItem(hwnd, 103);   /* the Help button */
            if (item)
                DestroyWindow(item);
        }

	keylist = hwnd;
	{
	    static int tabs[] = { 35, 75, 250 };
	    SendDlgItemMessage(hwnd, 100, LB_SETTABSTOPS,
			       sizeof(tabs) / sizeof(*tabs),
			       (LPARAM) tabs);
	}
	keylist_update();
	return 0;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	  case IDCANCEL:
	    keylist = NULL;
	    DestroyWindow(hwnd);
	    return 0;
	  case 101:		       /* add key */
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		if (passphrase_box) {
		    MessageBeep(MB_ICONERROR);
		    SetForegroundWindow(passphrase_box);
		    break;
		}
		prompt_add_keyfile();
	    }
	    return 0;
	  case 102:		       /* remove key */
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int i;
		int rCount, sCount;
		int *selectedArray;
		
		/* our counter within the array of selected items */
		int itemNum;
		
		/* get the number of items selected in the list */
		int numSelected = 
			SendDlgItemMessage(hwnd, 100, LB_GETSELCOUNT, 0, 0);
		
		/* none selected? that was silly */
		if (numSelected == 0) {
		    MessageBeep(0);
		    break;
		}

		/* get item indices in an array */
		selectedArray = snewn(numSelected, int);
		SendDlgItemMessage(hwnd, 100, LB_GETSELITEMS,
				numSelected, (WPARAM)selectedArray);
		
		itemNum = numSelected - 1;
		rCount = pageant_count_ssh1_keys();
		sCount = pageant_count_ssh2_keys();
		
		/* go through the non-rsakeys until we've covered them all, 
		 * and/or we're out of selected items to check. note that
		 * we go *backwards*, to avoid complications from deleting
		 * things hence altering the offset of subsequent items
		 */
                for (i = sCount - 1; (itemNum >= 0) && (i >= 0); i--) {
                    skey = pageant_nth_ssh2_key(i);
			
                    if (selectedArray[itemNum] == rCount + i) {
                        pageant_delete_ssh2_key(skey);
                        ssh_key_free(skey->key);
                        sfree(skey);
                        itemNum--;
                    }
		}
		
		/* do the same for the rsa keys */
		for (i = rCount - 1; (itemNum >= 0) && (i >= 0); i--) {
                    rkey = pageant_nth_ssh1_key(i);

                    if(selectedArray[itemNum] == i) {
                        pageant_delete_ssh1_key(rkey);
                        freersakey(rkey);
                        sfree(rkey);
                        itemNum--;
                    }
		}

		sfree(selectedArray); 
		keylist_update();
	    }
	    return 0;
	  case 103:		       /* help */
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED) {
		launch_help(hwnd, WINHELP_CTX_pageant_general);
            }
	    return 0;
	}
	return 0;
      case WM_HELP:
        {
            int id = ((LPHELPINFO)lParam)->iCtrlId;
            const char *topic = NULL;
            switch (id) {
              case 100: topic = WINHELP_CTX_pageant_keylist; break;
              case 101: topic = WINHELP_CTX_pageant_addkey; break;
              case 102: topic = WINHELP_CTX_pageant_remkey; break;
            }
            if (topic) {
		launch_help(hwnd, topic);
            } else {
                MessageBeep(0);
            }
        }
        break;
      case WM_CLOSE:
	keylist = NULL;
	DestroyWindow(hwnd);
	return 0;
    }
    return 0;
}

/* Set up a system tray icon */
static BOOL AddTrayIcon(HWND hwnd)
{
    BOOL res;
    NOTIFYICONDATA tnid;
    HICON hicon;

#ifdef NIM_SETVERSION
    tnid.uVersion = 0;
    res = Shell_NotifyIcon(NIM_SETVERSION, &tnid);
#endif

    tnid.cbSize = sizeof(NOTIFYICONDATA);
    tnid.hWnd = hwnd;
    tnid.uID = 1;	       /* unique within this systray use */
    tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tnid.uCallbackMessage = WM_SYSTRAY;
#ifdef MOD_INTEGRATED_AGENT
    tnid.hIcon = hicon = LoadIcon(hinst, MAKEINTRESOURCE(901));
#else
    tnid.hIcon = hicon = LoadIcon(hinst, MAKEINTRESOURCE(201));
#endif
#ifdef MOD_PERSO
    strcpy(tnid.szTip, "Kageant (KiTTY authentication agent)");
#else
    strcpy(tnid.szTip, "Pageant (PuTTY authentication agent)");
#endif

    res = Shell_NotifyIcon(NIM_ADD, &tnid);

#ifdef MOD_PERSO
	trayIcone = tnid ;
#endif

    if (hicon) DestroyIcon(hicon);
    
    return res;
}

#ifdef MOD_PERSO
#include <sys/types.h>
#include <dirent.h>
#ifndef MOD_INTEGRATED_AGENT
// Teste l'existance d'un repertoire
int existdirectory( const char * filename ) {
	struct _stat statBuf ;
	
	if( filename == NULL ) return 0 ;
	if( strlen(filename)==0 ) return 0 ;
	if( _stat( filename, &statBuf ) == -1 ) return 0 ;
	
	if( ( statBuf.st_mode & _S_IFMT ) == _S_IFDIR ) { return 1 ; }
	else { return 0 ; }
}
#endif
int MakeSessionMenu( HMENU session_menu, const int start_menuitem, const char * directory ) {
	int index_menu = start_menuitem ;
	MENUITEMINFO mii;
		char buffer[MAX_PATH],fList[MAX_PATH];
		DIR * dir ;
		struct dirent * de ;
		if( strlen(directory)>0 ) {
			sprintf( buffer, ".\\Sessions\\%s", directory ) ;
		} else {
			sprintf( buffer, ".\\Sessions" ) ;
		}
		if( (dir=opendir(buffer)) != NULL ) {
			while( (de=readdir(dir)) != NULL ) 
			if( strcmp(de->d_name, ".")&&strcmp(de->d_name, "..") ) {
				unmungestr( de->d_name, fList, MAX_PATH ) ;
				char *newpath;
				newpath = (char*)malloc( strlen(buffer)+strlen(de->d_name)+2 );
				sprintf(newpath,"%s\\%s",buffer,de->d_name);
				if( existdirectory(newpath) ) {
					HMENU sub = CreateMenu();
					if( strlen(directory)>0 ) {
						sprintf(newpath,"%s/%s",directory,de->d_name);
					} else {
						sprintf(newpath,"%s",de->d_name);
					}
					index_menu=MakeSessionMenu(sub,index_menu,newpath);
					AppendMenu( session_menu, MF_POPUP, (UINT_PTR)sub, fList ) ;
					index_menu++;
				} else if( !strcmp(FileExtension,"") || !strcmp(FileExtension,fList+strlen(fList)-strlen(FileExtension)) ) {
					memset(&mii, 0, sizeof(mii));
					mii.cbSize = sizeof(mii);
					mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
					mii.fType = MFT_STRING;
					mii.fState = MFS_ENABLED;
					mii.wID = (index_menu * 16) + IDM_SESSIONS_BASE;
					if( strlen(directory)>0 ) {
						sprintf(newpath,"%s/%s",directory,fList);
					} else {
						sprintf(newpath,"%s",fList);
					}
					mii.dwTypeData = newpath;
					InsertMenuItem(session_menu, index_menu, true, &mii);
					index_menu++;
				}
				free(newpath);
			}
		}
	return index_menu ;
}
#endif

/* Update the saved-sessions menu. */
static void update_sessions(void)
{
    int num_entries;
    HKEY hkey;
    TCHAR buf[MAX_PATH + 1];
    MENUITEMINFO mii;
    strbuf *sb;

    int index_key, index_menu;

    if (!putty_path)
	return;

#ifdef MOD_PERSO
    if( IniFileFlag==2 ) {

      for(num_entries = GetMenuItemCount(session_menu);
	num_entries > initial_menuitems_count;
	num_entries--)
	RemoveMenu(session_menu, 0, MF_BYPOSITION);
    
	index_menu = 0;
        index_menu = MakeSessionMenu( session_menu, index_menu, "" ) ;


    } else {
#endif
    if(ERROR_SUCCESS != RegOpenKey(HKEY_CURRENT_USER, PUTTY_REGKEY, &hkey))
	return;

    for(num_entries = GetMenuItemCount(session_menu);
	num_entries > initial_menuitems_count;
	num_entries--)
	RemoveMenu(session_menu, 0, MF_BYPOSITION);

    index_key = 0;
    index_menu = 0;

    sb = strbuf_new();
    while(ERROR_SUCCESS == RegEnumKey(hkey, index_key, buf, MAX_PATH)) {
	if(strcmp(buf, PUTTY_DEFAULT) != 0) {
            strbuf_clear(sb);
            unescape_registry_key(buf, sb);

	    memset(&mii, 0, sizeof(mii));
	    mii.cbSize = sizeof(mii);
	    mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
	    mii.fType = MFT_STRING;
	    mii.fState = MFS_ENABLED;
	    mii.wID = (index_menu * 16) + IDM_SESSIONS_BASE;
	    mii.dwTypeData = sb->s;
	    InsertMenuItem(session_menu, index_menu, true, &mii);
	    index_menu++;
	}
	index_key++;
    }
    strbuf_free(sb);
    RegCloseKey(hkey);

#ifdef MOD_PERSO
    }
#endif

    if(index_menu == 0) {
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_TYPE | MIIM_STATE;
	mii.fType = MFT_STRING;
	mii.fState = MFS_GRAYED;
	mii.dwTypeData = _T("(No sessions)");
	InsertMenuItem(session_menu, index_menu, true, &mii);
    }
}

#ifndef NO_SECURITY
/*
 * Versions of Pageant prior to 0.61 expected this SID on incoming
 * communications. For backwards compatibility, and more particularly
 * for compatibility with derived works of PuTTY still using the old
 * Pageant client code, we accept it as an alternative to the one
 * returned from get_user_sid() in winpgntc.c.
 */
PSID get_default_sid(void)
{
    HANDLE proc = NULL;
    DWORD sidlen;
    PSECURITY_DESCRIPTOR psd = NULL;
    PSID sid = NULL, copy = NULL, ret = NULL;

    if ((proc = OpenProcess(MAXIMUM_ALLOWED, false,
                            GetCurrentProcessId())) == NULL)
        goto cleanup;

    if (p_GetSecurityInfo(proc, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                          &sid, NULL, NULL, NULL, &psd) != ERROR_SUCCESS)
        goto cleanup;

    sidlen = GetLengthSid(sid);

    copy = (PSID)smalloc(sidlen);

    if (!CopySid(sidlen, copy, sid))
        goto cleanup;

    /* Success. Move sid into the return value slot, and null it out
     * to stop the cleanup code freeing it. */
    ret = copy;
    copy = NULL;

  cleanup:
    if (proc != NULL)
        CloseHandle(proc);
    if (psd != NULL)
        LocalFree(psd);
    if (copy != NULL)
        sfree(copy);

    return ret;
}
#endif

struct PageantReply {
    char *buf;
    size_t size, len;
    bool overflowed;
    BinarySink_IMPLEMENTATION;
};

static void pageant_reply_BinarySink_write(
    BinarySink *bs, const void *data, size_t len)
{
    struct PageantReply *rep = BinarySink_DOWNCAST(bs, struct PageantReply);
    if (!rep->overflowed && len <= rep->size - rep->len) {
        memcpy(rep->buf + rep->len, data, len);
        rep->len += len;
    } else {
        rep->overflowed = true;
    }
}

static char *answer_filemapping_message(const char *mapname)
{
    HANDLE maphandle = INVALID_HANDLE_VALUE;
    void *mapaddr = NULL;
    char *err = NULL;
    size_t mapsize;
    unsigned msglen;
    struct PageantReply reply;

#ifndef NO_SECURITY
    PSID mapsid = NULL;
    PSID expectedsid = NULL;
    PSID expectedsid_bc = NULL;
    PSECURITY_DESCRIPTOR psd = NULL;
#endif

    reply.buf = NULL;

#ifdef DEBUG_IPC
    debug("mapname = \"%s\"\n", mapname);
#endif

    maphandle = OpenFileMapping(FILE_MAP_ALL_ACCESS, false, mapname);
    if (maphandle == NULL || maphandle == INVALID_HANDLE_VALUE) {
        err = dupprintf("OpenFileMapping(\"%s\"): %s",
                        mapname, win_strerror(GetLastError()));
        goto cleanup;
    }

#ifdef DEBUG_IPC
    debug("maphandle = %p\n", maphandle);
#endif

#ifndef NO_SECURITY
    if (has_security) {
        DWORD retd;

        if ((expectedsid = get_user_sid()) == NULL) {
            err = dupstr("unable to get user SID");
            goto cleanup;
        }

        if ((expectedsid_bc = get_default_sid()) == NULL) {
            err = dupstr("unable to get default SID");
            goto cleanup;
        }

        if ((retd = p_GetSecurityInfo(
                 maphandle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                 &mapsid, NULL, NULL, NULL, &psd) != ERROR_SUCCESS)) {
            err = dupprintf("unable to get owner of file mapping: "
                            "GetSecurityInfo returned: %s",
                            win_strerror(retd));
            goto cleanup;
        }

#ifdef DEBUG_IPC
        {
            LPTSTR ours, ours2, theirs;
            ConvertSidToStringSid(mapsid, &theirs);
            ConvertSidToStringSid(expectedsid, &ours);
            ConvertSidToStringSid(expectedsid_bc, &ours2);
            debug("got sids:\n  oursnew=%s\n  oursold=%s\n"
                  "  theirs=%s\n", ours, ours2, theirs);
            LocalFree(ours);
            LocalFree(ours2);
            LocalFree(theirs);
        }
#endif

        if (!EqualSid(mapsid, expectedsid) &&
            !EqualSid(mapsid, expectedsid_bc)) {
            err = dupstr("wrong owning SID of file mapping");
            goto cleanup;
        }
    } else
#endif /* NO_SECURITY */
    {
#ifdef DEBUG_IPC
        debug("security APIs not present\n");
#endif
    }

    mapaddr = MapViewOfFile(maphandle, FILE_MAP_WRITE, 0, 0, 0);
    if (!mapaddr) {
        err = dupprintf("unable to obtain view of file mapping: %s",
                        win_strerror(GetLastError()));
        goto cleanup;
    }

#ifdef DEBUG_IPC
    debug("mapped address = %p\n", mapaddr);
#endif

    {
        MEMORY_BASIC_INFORMATION mbi;
        size_t mbiSize = VirtualQuery(mapaddr, &mbi, sizeof(mbi));
        if (mbiSize == 0) {
            err = dupprintf("unable to query view of file mapping: %s",
                            win_strerror(GetLastError()));
            goto cleanup;
        }
        if (mbiSize < (offsetof(MEMORY_BASIC_INFORMATION, RegionSize) +
                       sizeof(mbi.RegionSize))) {
            err = dupstr("VirtualQuery returned too little data to get "
                         "region size");
            goto cleanup;
        }

        mapsize = mbi.RegionSize;
    }
#ifdef DEBUG_IPC
    debug("region size = %"SIZEu"\n", mapsize);
#endif
    if (mapsize < 5) {
        err = dupstr("mapping smaller than smallest possible request");
        goto cleanup;
    }

    msglen = GET_32BIT_MSB_FIRST((unsigned char *)mapaddr);

#ifdef DEBUG_IPC
    debug("msg length=%08x, msg type=%02x\n",
          msglen, (unsigned)((unsigned char *) mapaddr)[4]);
#endif

    reply.buf = (char *)mapaddr + 4;
    reply.size = mapsize - 4;
    reply.len = 0;
    reply.overflowed = false;
    BinarySink_INIT(&reply, pageant_reply_BinarySink_write);

    if (msglen > mapsize - 4) {
        pageant_failure_msg(BinarySink_UPCAST(&reply),
                            "incoming length field too large", NULL, NULL);
    } else {
        pageant_handle_msg(BinarySink_UPCAST(&reply),
                           (unsigned char *)mapaddr + 4, msglen, NULL, NULL);
        if (reply.overflowed) {
            reply.len = 0;
            reply.overflowed = false;
            pageant_failure_msg(BinarySink_UPCAST(&reply),
                                "output would overflow message buffer",
                                NULL, NULL);
        }
    }

    if (reply.overflowed) {
        err = dupstr("even failure message overflows buffer");
        goto cleanup;
    }

    /* Write in the initial length field, and we're done. */
    PUT_32BIT_MSB_FIRST(((unsigned char *)mapaddr), reply.len);

  cleanup:
    /* expectedsid has the lifetime of the program, so we don't free it */
    sfree(expectedsid_bc);
    if (psd)
        LocalFree(psd);
    if (mapaddr)
        UnmapViewOfFile(mapaddr);
    if (maphandle != NULL && maphandle != INVALID_HANDLE_VALUE)
        CloseHandle(maphandle);
    return err;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    static bool menuinprogress;
    static UINT msgTaskbarCreated = 0;

    switch (message) {
      case WM_CREATE:
        msgTaskbarCreated = RegisterWindowMessage(_T("TaskbarCreated"));
        break;
      default:
        if (message==msgTaskbarCreated) {
            /*
	     * Explorer has been restarted, so the tray icon will
	     * have been lost.
	     */
	    AddTrayIcon(hwnd);
        }
        break;
        
      case WM_SYSTRAY:
	if (lParam == WM_RBUTTONUP) {
	    POINT cursorpos;
	    GetCursorPos(&cursorpos);
	    PostMessage(hwnd, WM_SYSTRAY2, cursorpos.x, cursorpos.y);
	} else if (lParam == WM_LBUTTONDBLCLK) {
	    /* Run the default menu item. */
	    UINT menuitem = GetMenuDefaultItem(systray_menu, false, 0);
	    if (menuitem != -1)
		PostMessage(hwnd, WM_COMMAND, menuitem, 0);
	}
	break;
      case WM_SYSTRAY2:
	if (!menuinprogress) {
	    menuinprogress = true;
	    update_sessions();
	    SetForegroundWindow(hwnd);
	    TrackPopupMenu(systray_menu,
			   TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
			   TPM_RIGHTBUTTON,
			   wParam, lParam, 0, hwnd, NULL);
	    menuinprogress = false;
	}
	break;
      case WM_COMMAND:
      case WM_SYSCOMMAND:
	switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
	  case IDM_PUTTY:
            {
                TCHAR cmdline[10];
                cmdline[0] = '\0';
                if (restrict_putty_acl)
                    strcat(cmdline, "&R");

                if((INT_PTR)ShellExecute(hwnd, NULL, putty_path, cmdline,
                                         _T(""), SW_SHOW) <= 32) {
                    MessageBox(NULL, "Unable to execute PuTTY!",
                               "Error", MB_OK | MB_ICONERROR);
                }
            }
	    break;
	  case IDM_CLOSE:
	    if (passphrase_box)
		SendMessage(passphrase_box, WM_CLOSE, 0, 0);
	    SendMessage(hwnd, WM_CLOSE, 0, 0);
	    break;
	  case IDM_VIEWKEYS:
	    if (!keylist) {
		keylist = CreateDialog(hinst, MAKEINTRESOURCE(211),
				       NULL, KeyListProc);
		ShowWindow(keylist, SW_SHOWNORMAL);
	    }
	    /* 
	     * Sometimes the window comes up minimised / hidden for
	     * no obvious reason. Prevent this. This also brings it
	     * to the front if it's already present (the user
	     * selected View Keys because they wanted to _see_ the
	     * thing).
	     */
	    SetForegroundWindow(keylist);
	    SetWindowPos(keylist, HWND_TOP, 0, 0, 0, 0,
			 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	    break;
	  case IDM_ADDKEY:
	    if (passphrase_box) {
		MessageBeep(MB_ICONERROR);
		SetForegroundWindow(passphrase_box);
		break;
	    }
	    prompt_add_keyfile();
	    break;
	  case IDM_ABOUT:
	    if (!aboutbox) {
		aboutbox = CreateDialog(hinst, MAKEINTRESOURCE(213),
					NULL, AboutProc);
		ShowWindow(aboutbox, SW_SHOWNORMAL);
		/* 
		 * Sometimes the window comes up minimised / hidden
		 * for no obvious reason. Prevent this.
		 */
		SetForegroundWindow(aboutbox);
		SetWindowPos(aboutbox, HWND_TOP, 0, 0, 0, 0,
			     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	    }
	    break;
	  case IDM_HELP:
	    launch_help(hwnd, WINHELP_CTX_pageant_general);
	    break;
	  default:
	    {
		if(wParam >= IDM_SESSIONS_BASE && wParam <= IDM_SESSIONS_MAX) {
		    MENUITEMINFO mii;
		    TCHAR buf[MAX_PATH + 1];
		    TCHAR param[MAX_PATH + 1];
		    memset(&mii, 0, sizeof(mii));
		    mii.cbSize = sizeof(mii);
		    mii.fMask = MIIM_TYPE;
		    mii.cch = MAX_PATH;
		    mii.dwTypeData = buf;
		    GetMenuItemInfo(session_menu, wParam, false, &mii);
                    param[0] = '\0';
                    if (restrict_putty_acl)
                        strcat(param, "&R");
		    strcat(param, "@");
		    strcat(param, mii.dwTypeData);
//MessageBox(NULL,param,"Launching",MB_OK) ;
		    if((INT_PTR)ShellExecute(hwnd, NULL, putty_path, param,
					 _T(""), SW_SHOW) <= 32) {
			MessageBox(NULL, "Unable to execute PuTTY!", "Error",
				   MB_OK | MB_ICONERROR);
		    }
		}
	    }
	    break;
	}
	break;
      case WM_DESTROY:
	quit_help(hwnd);
	PostQuitMessage(0);
	return 0;
      case WM_COPYDATA:
	{
	    COPYDATASTRUCT *cds;
	    char *mapname, *err;

	    cds = (COPYDATASTRUCT *) lParam;
	    if (cds->dwData != AGENT_COPYDATA_ID)
		return 0;	       /* not our message, mate */
	    mapname = (char *) cds->lpData;
	    if (mapname[cds->cbData - 1] != '\0')
		return 0;	       /* failure to be ASCIZ! */
            err = answer_filemapping_message(mapname);
            if (err) {
#ifdef DEBUG_IPC
                debug("IPC failed: %s\n", err);
#endif
                sfree(err);
                return 0;
            }
	    return 1;
	}
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/*
 * Fork and Exec the command in cmdline. [DBW]
 */
void spawn_cmd(const char *cmdline, const char *args, int show)
{
    if (ShellExecute(NULL, _T("open"), cmdline,
		     args, NULL, show) <= (HINSTANCE) 32) {
	char *msg;
	msg = dupprintf("Failed to run \"%s\": %s", cmdline,
			win_strerror(GetLastError()));
	MessageBox(NULL, msg, APPNAME, MB_OK | MB_ICONEXCLAMATION);
	sfree(msg);
    }
}

#ifndef MOD_INTEGRATED_AGENT
void agent_schedule_callback(void (*callback)(void *, void *, int),
			     void *callback_ctx, void *data, int len)
{
    unreachable("all Pageant's own agent requests should be synchronous");
}

void cleanup_exit(int code)
{
    shutdown_help();
    exit(code);
}
#endif

int flags = FLAG_SYNCAGENT;


#ifndef MOD_INTEGRATED_AGENT
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
#else
int WINAPI Agent_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
#endif
{
    WNDCLASS wndclass;
    MSG msg;
    const char *command = NULL;
    bool added_keys = false;
    int argc, i;
    char **argv, **argstart;

    dll_hijacking_protection();

    hinst = inst;
    hwnd = NULL;

#ifdef MOD_PERSO
	IniFileFlag = 0 ;
	DirectoryBrowseFlag = 0 ;
	LoadParametersLight() ;
#endif

    /*
     * Determine whether we're an NT system (should have security
     * APIs) or a non-NT system (don't do security).
     */
    init_winver();
    has_security = (osPlatformId == VER_PLATFORM_WIN32_NT);

    if (has_security) {
#ifndef NO_SECURITY
	/*
	 * Attempt to get the security API we need.
	 */
        if (!got_advapi()) {
	    MessageBox(NULL,
		       "Unable to access security APIs. Pageant will\n"
		       "not run, in case it causes a security breach.",
		       "Pageant Fatal Error", MB_ICONERROR | MB_OK);
	    return 1;
	}
#else
	MessageBox(NULL,
		   "This program has been compiled for Win9X and will\n"
		   "not run on NT, in case it causes a security breach.",
		   "Pageant Fatal Error", MB_ICONERROR | MB_OK);
	return 1;
#endif
    }

    /*
     * See if we can find our Help file.
     */
    init_help();

    /*
     * Look for the PuTTY binary (we will enable the saved session
     * submenu if we find it).
     */
    {
        char b[2048], *p, *q, *r;
        FILE *fp;
        GetModuleFileName(NULL, b, sizeof(b) - 16);
        r = b;
        p = strrchr(b, '\\');
        if (p && p >= r) r = p+1;
        q = strrchr(b, ':');
        if (q && q >= r) r = q+1;
        strcpy(r, "putty.exe");
        if ( (fp = fopen(b, "r")) != NULL) {
            putty_path = dupstr(b);
            fclose(fp);
#ifdef MOD_PERSO
	} else {
		strcpy(r, "kitty.exe");
		if ( (fp = fopen(b, "r")) != NULL) {
			putty_path = dupstr(b);
			fclose(fp);
		} else {
			strcpy(r, "kitty_portable.exe");
			if ( (fp = fopen(b, "r")) != NULL) {
				putty_path = dupstr(b);
				fclose(fp);
			} else putty_path = NULL;
		}
	}
#else
        } else
            putty_path = NULL;
#endif
    }

    /*
     * Find out if Pageant is already running.
     */
    already_running = agent_exists();

    /*
     * Initialise the cross-platform Pageant code.
     */
    if (!already_running) {
        pageant_init();
    }

    /*
     * Process the command line and add keys as listed on it.
     */
    split_into_argv(cmdline, &argc, &argv, &argstart);
    for (i = 0; i < argc; i++) {
	if (!strcmp(argv[i], "-pgpfp")) {
	    pgp_fingerprints();
	    return 1;
        } else if (!strcmp(argv[i], "-restrict-acl") ||
                   !strcmp(argv[i], "-restrict_acl") ||
                   !strcmp(argv[i], "-restrictacl")) {
            restrict_process_acl();
        } else if (!strcmp(argv[i], "-restrict-putty-acl") ||
                   !strcmp(argv[i], "-restrict_putty_acl")) {
            restrict_putty_acl = true;
#ifdef MOD_PERSO
	} else if (!strcmp(argv[i], "-pass")) {
		i++;
		strcpy(pphrase,argv[i]);
#endif
	} else if (!strcmp(argv[i], "-c")) {
	    /*
	     * If we see `-c', then the rest of the
	     * command line should be treated as a
	     * command to be spawned.
	     */
	    if (i < argc-1)
		command = argstart[i+1];
	    else
		command = "";
	    break;
	} else {
            Filename *fn = filename_from_str(argv[i]);
	    win_add_keyfile(fn);
            filename_free(fn);
	    added_keys = true;
	}
    }

    /*
     * Forget any passphrase that we retained while going over
     * command line keyfiles.
     */
    pageant_forget_passphrases();

    if (command) {
	char *args;
	if (command[0] == '"')
	    args = strchr(++command, '"');
	else
	    args = strchr(command, ' ');
	if (args) {
	    *args++ = 0;
	    while(*args && isspace(*args)) args++;
	}
	spawn_cmd(command, args, show);
    }

    /*
     * If Pageant was already running, we leave now. If we haven't
     * even taken any auxiliary action (spawned a command or added
     * keys), complain.
     */
    if (already_running) {
	if (!command && !added_keys) {
	    MessageBox(NULL, "Pageant is already running", "Pageant Error",
		       MB_ICONERROR | MB_OK);
	}
	return 0;
    }

    if (!prev) {
	wndclass.style = 0;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = inst;
	wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
	wndclass.hCursor = LoadCursor(NULL, IDC_IBEAM);
	wndclass.hbrBackground = GetStockObject(BLACK_BRUSH);
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = APPNAME;

	RegisterClass(&wndclass);
    }

    keylist = NULL;

    hwnd = CreateWindow(APPNAME, APPNAME,
			WS_OVERLAPPEDWINDOW | WS_VSCROLL,
			CW_USEDEFAULT, CW_USEDEFAULT,
			100, 100, NULL, NULL, inst, NULL);

    /* Set up a system tray icon */
    AddTrayIcon(hwnd);

    /* Accelerators used: nsvkxa */
    systray_menu = CreatePopupMenu();
    if (putty_path) {
	session_menu = CreateMenu();
	AppendMenu(systray_menu, MF_ENABLED, IDM_PUTTY, "&New Session");
	AppendMenu(systray_menu, MF_POPUP | MF_ENABLED,
		   (UINT_PTR) session_menu, "&Saved Sessions");
	AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    }
    AppendMenu(systray_menu, MF_ENABLED, IDM_VIEWKEYS,
	   "&View Keys");
    AppendMenu(systray_menu, MF_ENABLED, IDM_ADDKEY, "Add &Key");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    if (has_help())
	AppendMenu(systray_menu, MF_ENABLED, IDM_HELP, "&Help");
    AppendMenu(systray_menu, MF_ENABLED, IDM_ABOUT, "&About");
    AppendMenu(systray_menu, MF_SEPARATOR, 0, 0);
    AppendMenu(systray_menu, MF_ENABLED, IDM_CLOSE, "E&xit");
    initial_menuitems_count = GetMenuItemCount(session_menu);

    /* Set the default menu item. */
    SetMenuDefaultItem(systray_menu, IDM_VIEWKEYS, false);

    ShowWindow(hwnd, SW_HIDE);

    /*
     * Main message loop.
     */
    while (GetMessage(&msg, NULL, 0, 0) == 1) {
	if (!(IsWindow(keylist) && IsDialogMessage(keylist, &msg)) &&
	    !(IsWindow(aboutbox) && IsDialogMessage(aboutbox, &msg))) {
	    TranslateMessage(&msg);
	    DispatchMessage(&msg);
	}
    }

    /* Clean up the system tray icon */
    {
	NOTIFYICONDATA tnid;

	tnid.cbSize = sizeof(NOTIFYICONDATA);
	tnid.hWnd = hwnd;
	tnid.uID = 1;

	Shell_NotifyIcon(NIM_DELETE, &tnid);

	DestroyMenu(systray_menu);
    }

    if (keypath) filereq_free(keypath);

    cleanup_exit(msg.wParam);
    return msg.wParam;		       /* just in case optimiser complains */
}
