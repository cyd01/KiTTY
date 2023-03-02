/*
 * windlg.c - dialogs for PuTTY(tel), including the configuration dialog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>

#include "putty.h"
#include "ssh.h"
#include "putty-rc.h"
#include "winseat.h"
#include "storage.h"
#include "dialog.h"
#include "licence.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#ifdef MSVC4
#define TVINSERTSTRUCT  TV_INSERTSTRUCT
#define TVITEM          TV_ITEM
#define ICON_BIG        1
#endif

/*
 * These are the various bits of data required to handle the
 * portable-dialog stuff in the config box. Having them at file
 * scope in here isn't too bad a place to put them; if we were ever
 * to need more than one config box per process we could always
 * shift them to a per-config-box structure stored in GWL_USERDATA.
 */
static struct controlbox *ctrlbox;
/*
 * ctrls_base holds the OK and Cancel buttons: the controls which
 * are present in all dialog panels. ctrls_panel holds the ones
 * which change from panel to panel.
 */
static struct winctrls ctrls_base, ctrls_panel;
static struct dlgparam dp;

#define LOGEVENT_INITIAL_MAX 128
#define LOGEVENT_CIRCULAR_MAX 128

static char *events_initial[LOGEVENT_INITIAL_MAX];
static char *events_circular[LOGEVENT_CIRCULAR_MAX];
static int ninitial = 0, ncircular = 0, circular_first = 0;


#ifdef MOD_PERSO
#include <math.h>
#include <process.h>
#include "kitty.h"
#include "kitty_commun.h"
#include "kitty_registry.h"

extern char BuildVersionTime[256] ;

void CenterDlgInParent(HWND hDlg) ;
int get_param( const char * val ) ;
void CheckVersionFromWebSite( HWND hwnd ) ;
bool SessPathIsInitial( void ) ;
bool IsThereDefaultSessionFile( void ) ;

#ifndef TIMER_SLIDEBG
//#define TIMER_SLIDEBG 12343
#define TIMER_SLIDEBG 8703
#endif

static char *getevent(int i) ;
int print_event_log( FILE * fp, int i ) {
	char * pst = getevent(i) ;
	if( pst == NULL ) { return 0 ; }
	fprintf( fp, "%s\n", pst ) ;
	return 1 ;
	}
void do_eventlog( const char * st ) ;

#endif
#ifdef MOD_PRINTCLIP
#define PRINT_TO_CLIPBOARD_STRING "Windows clipboard"
#endif

#define PRINTER_DISABLED_STRING "None (printing disabled)"

void force_normal(HWND hwnd)
{
    static bool recurse = false;

    WINDOWPLACEMENT wp;

    if (recurse)
	return;
    recurse = true;

    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
	wp.showCmd = SW_SHOWNORMAL;
	SetWindowPlacement(hwnd, &wp);
    }
    recurse = false;
}

static char *getevent(int i)
{
    if (i < ninitial)
        return events_initial[i];
    if ((i -= ninitial) < ncircular)
        return events_circular[(circular_first + i) % LOGEVENT_CIRCULAR_MAX];
    return NULL;
}

static HWND logbox;
HWND event_log_window(void) { return logbox; }

static INT_PTR CALLBACK LogProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    int i;

    switch (msg) {
      case WM_INITDIALOG: {
	    char *str = dupprintf("%s Event Log", appname);
	    SetWindowText(hwnd, str);
	    sfree(str);
	    static int tabs[4] = { 78, 108 };
	    SendDlgItemMessage(hwnd, IDN_LIST, LB_SETTABSTOPS, 2,
			       (LPARAM) tabs);
	for (i = 0; i < ninitial; i++)
	    SendDlgItemMessage(hwnd, IDN_LIST, LB_ADDSTRING,
			       0, (LPARAM) events_initial[i]);
	for (i = 0; i < ncircular; i++)
	    SendDlgItemMessage(hwnd, IDN_LIST, LB_ADDSTRING,
			       0, (LPARAM) events_circular[(circular_first + i) % LOGEVENT_CIRCULAR_MAX]);
	return 1;
      }
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	  case IDCANCEL:
	    logbox = NULL;
	    SetActiveWindow(GetParent(hwnd));
	    DestroyWindow(hwnd);
	    return 0;
	  case IDN_COPY:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int selcount;
		int *selitems;
		selcount = SendDlgItemMessage(hwnd, IDN_LIST,
					      LB_GETSELCOUNT, 0, 0);
		if (selcount == 0) {   /* don't even try to copy zero items */
		    MessageBeep(0);
		    break;
		}

		selitems = snewn(selcount, int);
		if (selitems) {
		    int count = SendDlgItemMessage(hwnd, IDN_LIST,
						   LB_GETSELITEMS,
						   selcount,
						   (LPARAM) selitems);
		    int i;
		    int size;
		    char *clipdata;
		    static unsigned char sel_nl[] = SEL_NL;

		    if (count == 0) {  /* can't copy zero stuff */
			MessageBeep(0);
			break;
		    }

		    size = 0;
		    for (i = 0; i < count; i++)
			size +=
			    strlen(getevent(selitems[i])) + sizeof(sel_nl);

		    clipdata = snewn(size, char);
		    if (clipdata) {
			char *p = clipdata;
			for (i = 0; i < count; i++) {
			    char *q = getevent(selitems[i]);
			    int qlen = strlen(q);
			    memcpy(p, q, qlen);
			    p += qlen;
			    memcpy(p, sel_nl, sizeof(sel_nl));
			    p += sizeof(sel_nl);
			}
			write_aclip(CLIP_SYSTEM, clipdata, size, true);
			sfree(clipdata);
		    }
		    sfree(selitems);

		    for (i = 0; i < (ninitial + ncircular); i++)
			SendDlgItemMessage(hwnd, IDN_LIST, LB_SETSEL,
					   false, i);
		}
	    }
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	logbox = NULL;
	SetActiveWindow(GetParent(hwnd));
	DestroyWindow(hwnd);
	return 0;
    }
    return 0;
}

static INT_PTR CALLBACK LicenceProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
	    char *str = dupprintf("%s Licence", appname);
	    SetWindowText(hwnd, str);
	    sfree(str);
            SetDlgItemText(hwnd, IDA_TEXT, LICENCE_TEXT("\r\n\r\n"));
	return 1;
      }
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



#if (defined MOD_PERSO) && (!defined FLJ)

//static const char MESSAGE[] = "";
static const char MESSAGE[] = "                                                                                           KiTTY software is developed by Cyd for 9bis.com, copyright \251 2005-2023, thanks to Leo for bcrypt and mini libraries, thanks to all contributors                                                                                       " ;

static INT_PTR CALLBACK AboutProc(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{
    char *str;
    static HFONT hFontTitle ;
    static HFONT hFontHover ;
    static HFONT hFontNormal ;
    static BOOL hover_email;
    static BOOL capture_email;
    static BOOL hover_webpage;
    static BOOL capture_webpage;
    static HCURSOR hCursorNormal;
    static HCURSOR hCursorHover;
    static int message_timer = 1000 ;
    static char * mess = NULL ;
	
    switch (msg) {
	case WM_INITDIALOG: {
		char buffer[1024] ;
		LOGFONT lf;
#ifdef FLJ
		/* Positionnement du ssh handler */
		CreateSSHHandler() ;
		/* Creation des association de fichiers .ktx */
		CreateFileAssoc() ;
#endif
		
		sprintf( buffer, "KiTTY - %s", BuildVersionTime ) ;
		SetDlgItemText(hwnd,IDA_VERSION,buffer);
        
		str = dupprintf("About %s That's all folks !", appname);
		SetWindowText(hwnd, str);
		sfree(str);
        
		if (hFontTitle == NULL) {
			if (NULL == (hFontTitle = (HFONT)SendDlgItemMessage(hwnd,IDA_VERSION,WM_GETFONT,0,0)))
				hFontTitle = GetStockObject(DEFAULT_GUI_FONT);
			GetObject(hFontTitle,sizeof(LOGFONT),&lf);
			lf.lfWeight = FW_BOLD;
			hFontTitle = CreateFontIndirect(&lf);
		}
		//  Font setup
		if (NULL == (hFontHover = (HFONT)SendDlgItemMessage(hwnd,IDC_EMAIL,WM_GETFONT,0,0)))
			hFontHover = GetStockObject(DEFAULT_GUI_FONT);
		GetObject(hFontHover,sizeof(LOGFONT),&lf);
		lf.lfUnderline = TRUE;
		hFontNormal = CreateFontIndirect(&lf);
	
		//  Cursor setup
		hCursorNormal = LoadCursor( NULL, MAKEINTRESOURCE((DWORD)IDC_ARROW) ) ;
		if (!(hCursorHover = LoadCursor( NULL, (LPCTSTR)MAKEINTRESOURCE((DWORD)IDC_HAND) )))
			hCursorHover  = LoadCursor( GetModuleHandle(NULL), MAKEINTRESOURCE(IDC_HOVER) ) ;

		hover_email = FALSE;
		capture_email = FALSE;
		hover_webpage = FALSE;
		capture_webpage = FALSE;

		CenterDlgInParent(hwnd);
		
		mess = (char*)MESSAGE ;
		SetDlgItemText(hwnd,IDC_BAN,mess);
		if( strlen( mess ) > 0 ) SetTimer(hwnd, message_timer, 100, NULL) ;
		return 1; 
		}
		break ;
		
	case WM_TIMER:
		if ((UINT_PTR)wParam == message_timer) {
			mess++ ;
			SetDlgItemText(hwnd,IDC_BAN,mess);
			if( strlen( mess ) < strlen("                                                                                       ") ) mess = (char*)MESSAGE ;
			}
		break ;
	
	case WM_NCACTIVATE:
		if (!(BOOL)wParam) { //  we're not active, clear hover states
			hover_email = FALSE;
			capture_email = FALSE;
			hover_webpage = FALSE;
			capture_webpage = FALSE;
			InvalidateRect(GetDlgItem(hwnd,IDC_EMAIL),NULL,FALSE);
			InvalidateRect(GetDlgItem(hwnd,IDC_WEBPAGE),NULL,FALSE);
			}
		return FALSE;
	
	case WM_CTLCOLORSTATIC: {
		DWORD dwId = GetWindowLong((HWND)lParam,GWL_ID);
		HDC hdc = (HDC)wParam;

		if (dwId == IDA_VERSION) {
			SetBkMode(hdc,TRANSPARENT);
			SetTextColor(hdc,GetSysColor(COLOR_BTNTEXT));
			SelectObject(hdc,hFontTitle);
			return(LONG)GetSysColorBrush(COLOR_BTNFACE);
			}
		if (dwId == IDC_EMAIL || dwId == IDC_WEBPAGE) {
			SetBkMode(hdc,TRANSPARENT);
			if (GetSysColorBrush(26))
				SetTextColor(hdc,GetSysColor(26));
			else
				SetTextColor(hdc,RGB(0,0,255));
			if (dwId == IDC_EMAIL)
				SelectObject(hdc,hover_email?hFontHover:hFontNormal);
			else
				SelectObject(hdc,hover_webpage?hFontHover:hFontNormal);
			return(LONG)GetSysColorBrush(COLOR_BTNFACE);
			}
		}
		break ;
	
	case WM_MOUSEMOVE:  {
		POINT pt = { LOWORD(lParam), HIWORD(lParam) };
		HWND hwndHover = ChildWindowFromPoint(hwnd,pt);
		DWORD dwId = GetWindowLong(hwndHover,GWL_ID);

		if (GetActiveWindow() == hwnd) {
			if (wParam & MK_LBUTTON && !capture_email && !capture_webpage) {
				;
				}
			else if (hover_email != (dwId == IDC_EMAIL) && !capture_webpage) {
				hover_email = !hover_email;
				InvalidateRect(GetDlgItem(hwnd,IDC_EMAIL),NULL,FALSE);
				}
			else if (hover_webpage != (dwId == IDC_WEBPAGE) && !capture_email) {
				hover_webpage = !hover_webpage;
				InvalidateRect(GetDlgItem(hwnd,IDC_WEBPAGE),NULL,FALSE);
				}
			SetCursor((hover_email || hover_webpage)?hCursorHover:hCursorNormal);
			}
		}
		break;
	
	case WM_LBUTTONDOWN: {
		POINT pt = { LOWORD(lParam), HIWORD(lParam) };
		HWND hwndHover = ChildWindowFromPoint(hwnd,pt);
		DWORD dwId = GetWindowLong(hwndHover,GWL_ID);

		if (dwId == IDC_EMAIL) {
			GetCapture();
			hover_email = TRUE;
			capture_email = TRUE;
			InvalidateRect(GetDlgItem(hwnd,IDC_EMAIL),NULL,FALSE);
			}
		else if (dwId == IDC_WEBPAGE) {
			GetCapture();
			hover_webpage = TRUE;
			capture_webpage = TRUE;
			InvalidateRect(GetDlgItem(hwnd,IDC_WEBPAGE),NULL,FALSE);
			}
		SetCursor((hover_email || hover_webpage)?hCursorHover:hCursorNormal);
		}
		break;
	
	case WM_LBUTTONUP: {
		POINT pt = { LOWORD(lParam), HIWORD(lParam) };
		HWND hwndHover = ChildWindowFromPoint(hwnd,pt);
		DWORD dwId = GetWindowLong(hwndHover,GWL_ID);

		if (capture_email || capture_webpage) {
			ReleaseCapture();
			if (dwId == IDC_EMAIL && capture_email) {
				ShellExecute(hwnd,"open","mailto:kitty@9bis.com",NULL,NULL,SW_SHOWNORMAL);
				}
			else if (dwId == IDC_WEBPAGE && capture_webpage) {
				ShellExecute(hwnd,"open","https://www.9bis.net/kitty",NULL,NULL,SW_SHOWNORMAL);
				}
			capture_email = FALSE;
			capture_webpage = FALSE;
			}
		SetCursor((hover_email || hover_webpage)?hCursorHover:hCursorNormal);
		}
		break;
      
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	  case IDCANCEL:
		hover_email = FALSE;
		capture_email = FALSE;
		hover_webpage = FALSE;
		capture_webpage = FALSE;
	    KillTimer(hwnd, message_timer);
	    EndDialog(hwnd, TRUE);
	    return 0;
	  case IDA_LICENCE:
	    EnableWindow(hwnd, 0);
	    DialogBox(hinst, MAKEINTRESOURCE(IDD_LICENCEBOX),
		      hwnd, LicenceProc);
	    EnableWindow(hwnd, 1);
	    SetActiveWindow(hwnd);
	    return 0;

	  case IDA_WEB:
	    /* Load web browser */
	    CheckVersionFromWebSite( hwnd ) ;
/*	  {
		char buffer[1024]="", vers[1024]="" ;
		int i ;
		strcpy( vers, BuildVersionTime ) ;
		for( i = 0 ; i < strlen( vers ) ; i ++ ) {
			if( !(((vers[i]>='0')&&(vers[i]<='9'))||(vers[i]=='.')) ) { vers[i] = '\0' ; break ; }
			}
		sprintf( buffer, "https://www.9bis.net/kitty/check_update.php?version=%s", vers ) ;
		ShellExecute(hwnd, "open", buffer, 0, 0, SW_SHOWDEFAULT);
	  }*/
	    return 0;
	  case IDA_DON:
	    /* Load web browser */
	  {	char buffer[1024]="";
		sprintf( buffer, "https://www.9bis.net/kitty/#!pages/donation.md" ) ;
		ShellExecute(hwnd, "open", buffer, 0, 0, SW_SHOWDEFAULT);
	  }
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	KillTimer(hwnd, message_timer);
	EndDialog(hwnd, TRUE);
	return 0;
    }
    return 0;
}

static INT_PTR CALLBACK AboutProcOrig(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{

#else
static INT_PTR CALLBACK AboutProc(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{
#endif
    char *str;

    switch (msg) {
      case WM_INITDIALOG: {
	str = dupprintf("About %s", appname);
	SetWindowText(hwnd, str);
	sfree(str);
            char *buildinfo_text = buildinfo("\r\n");
            char *text = dupprintf
                ("%s\r\n\r\n%s\r\n\r\n%s\r\n\r\n%s",
                 appname, ver, buildinfo_text,
                 "\251 " SHORT_COPYRIGHT_DETAILS ". All rights reserved.");
            sfree(buildinfo_text);
            SetDlgItemText(hwnd, IDA_TEXT, text);
            MakeDlgItemBorderless(hwnd, IDA_TEXT);
            sfree(text);
#ifdef MOD_PERSO
	if( get_param("PUTTY") ) SetDlgItemText(hwnd, IDA_TEXT2, "" ) ;
#endif
	return 1;
        }
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	  case IDCANCEL:
	    EndDialog(hwnd, true);
	    return 0;
	  case IDA_LICENCE:
	    EnableWindow(hwnd, 0);
	    DialogBox(hinst, MAKEINTRESOURCE(IDD_LICENCEBOX),
		      hwnd, LicenceProc);
	    EnableWindow(hwnd, 1);
	    SetActiveWindow(hwnd);
	    return 0;

	  case IDA_WEB:
	    /* Load web browser */
	    ShellExecute(hwnd, "open",
			 "https://www.chiark.greenend.org.uk/~sgtatham/putty/",
			 0, 0, SW_SHOWDEFAULT);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog(hwnd, true);
	return 0;
    }
    return 0;
}

static int SaneDialogBox(HINSTANCE hinst,
			 LPCTSTR tmpl,
			 HWND hwndparent,
			 DLGPROC lpDialogFunc)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;
    int flags;
    int ret;
    int gm;

    wc.style = CS_DBLCLKS | CS_SAVEBITS | CS_BYTEALIGNWINDOW;
    wc.lpfnWndProc = DefDlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA + 2*sizeof(LONG_PTR);
    wc.hInstance = hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BACKGROUND +1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PuTTYConfigBox";
    RegisterClass(&wc);

    hwnd = CreateDialog(hinst, tmpl, hwndparent, lpDialogFunc);

    SetWindowLongPtr(hwnd, BOXFLAGS, 0); /* flags */
    SetWindowLongPtr(hwnd, BOXRESULT, 0); /* result from SaneEndDialog */

    while ((gm=GetMessage(&msg, NULL, 0, 0)) > 0) {
	flags=GetWindowLongPtr(hwnd, BOXFLAGS);
	if (!(flags & DF_END) && !IsDialogMessage(hwnd, &msg))
	    DispatchMessage(&msg);
	if (flags & DF_END)
	    break;
    }

    if (gm == 0)
        PostQuitMessage(msg.wParam); /* We got a WM_QUIT, pass it on */

    ret=GetWindowLongPtr(hwnd, BOXRESULT);
    DestroyWindow(hwnd);
    return ret;
}

static void SaneEndDialog(HWND hwnd, int ret)
{
    SetWindowLongPtr(hwnd, BOXRESULT, ret);
    SetWindowLongPtr(hwnd, BOXFLAGS, DF_END);
}

/*
 * Null dialog procedure.
 */
static INT_PTR CALLBACK NullDlgProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
    return 0;
}

enum {
    IDCX_ABOUT = IDC_ABOUT,
    IDCX_TVSTATIC,
    IDCX_TREEVIEW,
    IDCX_STDBASE,
    IDCX_PANELBASE = IDCX_STDBASE + 32
};

struct treeview_faff {
    HWND treeview;
    HTREEITEM lastat[4];
};

static HTREEITEM treeview_insert(struct treeview_faff *faff,
				 int level, char *text, char *path)
{
    TVINSERTSTRUCT ins;
    int i;
    HTREEITEM newitem;
    ins.hParent = (level > 0 ? faff->lastat[level - 1] : TVI_ROOT);
    ins.hInsertAfter = faff->lastat[level];
#if _WIN32_IE >= 0x0400 && defined NONAMELESSUNION
#define INSITEM DUMMYUNIONNAME.item
#else
#define INSITEM item
#endif
    ins.INSITEM.mask = TVIF_TEXT | TVIF_PARAM;
    ins.INSITEM.pszText = text;
    ins.INSITEM.cchTextMax = strlen(text)+1;
    ins.INSITEM.lParam = (LPARAM)path;
    newitem = TreeView_InsertItem(faff->treeview, &ins);
    if (level > 0)
	TreeView_Expand(faff->treeview, faff->lastat[level - 1],
			(level > 1 ? TVE_COLLAPSE : TVE_EXPAND));
    faff->lastat[level] = newitem;
    for (i = level + 1; i < 4; i++)
	faff->lastat[i] = NULL;
    return newitem;
}

/*
 * Create the panelfuls of controls in the configuration box.
 */
static void create_controls(HWND hwnd, char *path)
{
    struct ctlpos cp;
    int index;
    int base_id;
    struct winctrls *wc;

    if (!path[0]) {
	/*
	 * Here we must create the basic standard controls.
	 */
	ctlposinit(&cp, hwnd, 3, 3, 235);
	wc = &ctrls_base;
	base_id = IDCX_STDBASE;
    } else {
	/*
	 * Otherwise, we're creating the controls for a particular
	 * panel.
	 */
	ctlposinit(&cp, hwnd, 100, 3, 13);
	wc = &ctrls_panel;
	base_id = IDCX_PANELBASE;
    }

    for (index=-1; (index = ctrl_find_path(ctrlbox, path, index)) >= 0 ;) {
	struct controlset *s = ctrlbox->ctrlsets[index];
	winctrl_layout(&dp, wc, &cp, s, &base_id);
    }
}

/*
 * This function is the configuration box.
 * (Being a dialog procedure, in general it returns 0 if the default
 * dialog processing should be performed, and 1 if it should not.)
 */
static INT_PTR CALLBACK GenericMainDlgProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
    HWND hw, treeview;
    struct treeview_faff tvfaff;
    int ret;

    switch (msg) {
      case WM_INITDIALOG:
#ifdef MOD_PERSO
      {
	RECT rcClient ;
	int h ;
	GetWindowRect(hwnd, &rcClient) ;
      
        int NormalSize = 555 ;
	
	if( GetConfigBoxWindowHeight() > 0 ) { h = GetConfigBoxWindowHeight() ; }
	else if( GetConfigBoxHeight() >= 100 ) { h = GetConfigBoxHeight() ; }
	else {
		if( GetConfigBoxHeight() <= 7 ) { h = ceil(12*7+354) ; }
		else if( GetConfigBoxHeight() <= 16 ) { h = NormalSize ; }
		else {
			h = NormalSize + 12 * ( GetConfigBoxHeight()-16 ) ;
			if( h < NormalSize ) h = NormalSize ; 
			}
		}
	if( get_param("INIFILE")==SAVEMODE_DIR ) {
		h = h - 12 ; // il n'y a pas la liste déroulant des folders en mode portable
	}
#ifdef MOD_PROXY
	if( GetProxySelectionFlag() ) { h = h + 20 ; }
#endif
	// Initialise la taille de la ConfigBox (en cas de DPI speciaux)
	double ScaleY = GetDeviceCaps(GetDC(hwnd),LOGPIXELSY)/96.0 ; // La police standard (100%) vaut 96ppp (pixels per pouce)
	if( ScaleY!=1.0 ) { h = (int)( h*ScaleY ) ; }
	MoveWindow( hwnd, rcClient.left, rcClient.top, rcClient.right-rcClient.left, h, TRUE ) ;
	}
#endif
	dp.hwnd = hwnd;
	create_controls(hwnd, "");     /* Open and Cancel buttons etc */
	SetWindowText(hwnd, dp.wintitle);
	SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        if (has_help())
            SetWindowLongPtr(hwnd, GWL_EXSTYLE,
			     GetWindowLongPtr(hwnd, GWL_EXSTYLE) |
			     WS_EX_CONTEXTHELP);
        else {
            HWND item = GetDlgItem(hwnd, IDC_HELPBTN);
            if (item)
                DestroyWindow(item);
        }
	SendMessage(hwnd, WM_SETICON, (WPARAM) ICON_BIG,
		    (LPARAM) LoadIcon(hinst, MAKEINTRESOURCE(IDI_CFGICON)));
	/*
	 * Centre the window.
	 */
	{			       /* centre the window */
	    RECT rs, rd;

	    hw = GetDesktopWindow();
	    if (GetWindowRect(hw, &rs) && GetWindowRect(hwnd, &rd))
		MoveWindow(hwnd,
#ifdef MOD_PERSO
			   GetConfigBoxLeft()>0 ? GetConfigBoxLeft() : (rs.right + rs.left + rd.left - rd.right) / 2,
			   GetConfigBoxTop()>0 ? GetConfigBoxTop() : (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
#else
			   (rs.right + rs.left + rd.left - rd.right) / 2,
			   (rs.bottom + rs.top + rd.top - rd.bottom) / 2,
#endif
			   rd.right - rd.left, rd.bottom - rd.top, true);
	}

	/*
	 * Create the tree view.
	 */
	{
	    RECT r;
	    WPARAM font;
	    HWND tvstatic;

	    r.left = 3;
	    r.right = r.left + 95;
	    r.top = 3;
	    r.bottom = r.top + 10;
	    MapDialogRect(hwnd, &r);
	    tvstatic = CreateWindowEx(0, "STATIC", "Cate&gory:",
				      WS_CHILD | WS_VISIBLE,
				      r.left, r.top,
				      r.right - r.left, r.bottom - r.top,
				      hwnd, (HMENU) IDCX_TVSTATIC, hinst,
				      NULL);
	    font = SendMessage(hwnd, WM_GETFONT, 0, 0);
	    SendMessage(tvstatic, WM_SETFONT, font, MAKELPARAM(true, 0));

	    r.left = 3;
	    r.right = r.left + 95;
	    r.top = 13;
	    r.bottom = r.top + 219;
	    MapDialogRect(hwnd, &r);
	    treeview = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, "",
				      WS_CHILD | WS_VISIBLE |
				      WS_TABSTOP | TVS_HASLINES |
				      TVS_DISABLEDRAGDROP | TVS_HASBUTTONS
				      | TVS_LINESATROOT |
				      TVS_SHOWSELALWAYS, r.left, r.top,
				      r.right - r.left, r.bottom - r.top,
				      hwnd, (HMENU) IDCX_TREEVIEW, hinst,
				      NULL);
	    font = SendMessage(hwnd, WM_GETFONT, 0, 0);
	    SendMessage(treeview, WM_SETFONT, font, MAKELPARAM(true, 0));
	    tvfaff.treeview = treeview;
	    memset(tvfaff.lastat, 0, sizeof(tvfaff.lastat));
	}

	/*
	 * Set up the tree view contents.
	 */
	{
	    HTREEITEM hfirst = NULL;
	    int i;
	    char *path = NULL;
            char *firstpath = NULL;

	    for (i = 0; i < ctrlbox->nctrlsets; i++) {
		struct controlset *s = ctrlbox->ctrlsets[i];
		HTREEITEM item;
		int j;
		char *c;

		if (!s->pathname[0])
		    continue;
		j = path ? ctrl_path_compare(s->pathname, path) : 0;
		if (j == INT_MAX)
		    continue;	       /* same path, nothing to add to tree */

		/*
		 * We expect never to find an implicit path
		 * component. For example, we expect never to see
		 * A/B/C followed by A/D/E, because that would
		 * _implicitly_ create A/D. All our path prefixes
		 * are expected to contain actual controls and be
		 * selectable in the treeview; so we would expect
		 * to see A/D _explicitly_ before encountering
		 * A/D/E.
		 */
		assert(j == ctrl_path_elements(s->pathname) - 1);

		c = strrchr(s->pathname, '/');
		if (!c)
			c = s->pathname;
		else
			c++;

		item = treeview_insert(&tvfaff, j, c, s->pathname);
		if (!hfirst) {
		    hfirst = item;
                    firstpath = s->pathname;
                }

		path = s->pathname;
	    }

	    /*
	     * Put the treeview selection on to the first panel in the
	     * ctrlbox.
	     */
	    TreeView_SelectItem(treeview, hfirst);

            /*
             * And create the actual control set for that panel, to
             * match the initial treeview selection.
             */
            assert(firstpath);   /* config.c must have given us _something_ */
            create_controls(hwnd, firstpath);
	    dlg_refresh(NULL, &dp);    /* and set up control values */
	}

	/*
	 * Set focus into the first available control.
	 */
	{
	    int i;
	    struct winctrl *c;

	    for (i = 0; (c = winctrl_findbyindex(&ctrls_panel, i)) != NULL;
		 i++) {
		if (c->ctrl) {
		    dlg_set_focus(c->ctrl, &dp);
		    break;
		}
	    }
	}

        /*
         * Now we've finished creating our initial set of controls,
         * it's safe to actually show the window without risking setup
         * flicker.
         */
        ShowWindow(hwnd, SW_SHOWNORMAL);

        /*
         * Set the flag that activates a couple of the other message
         * handlers below, which were disabled until now to avoid
         * spurious firing during the above setup procedure.
         */
	SetWindowLongPtr(hwnd, GWLP_USERDATA, 1);
#ifdef MOD_PERSO
	//ShowWindow(hwnd,SW_HIDE);
	ShowWindow(hwnd,SW_SHOW);
#endif
	return 0;
      case WM_LBUTTONUP:
	/*
	 * Button release should trigger WM_OK if there was a
	 * previous double click on the session list.
	 */
	ReleaseCapture();
	if (dp.ended)
	    SaneEndDialog(hwnd, dp.endresult ? 1 : 0);
	break;
      case WM_NOTIFY:
	if (LOWORD(wParam) == IDCX_TREEVIEW &&
	    ((LPNMHDR) lParam)->code == TVN_SELCHANGED) {
            /*
             * Selection-change events on the treeview cause us to do
             * a flurry of control deletion and creation - but only
             * after WM_INITDIALOG has finished. The initial
             * selection-change event(s) during treeview setup are
             * ignored.
             */
	    HTREEITEM i;
	    TVITEM item;
	    char buffer[64];

            if (GetWindowLongPtr(hwnd, GWLP_USERDATA) != 1)
                return 0;

            i = TreeView_GetSelection(((LPNMHDR) lParam)->hwndFrom);
 
 	    SendMessage (hwnd, WM_SETREDRAW, false, 0);
 
	    item.hItem = i;
	    item.pszText = buffer;
	    item.cchTextMax = sizeof(buffer);
	    item.mask = TVIF_TEXT | TVIF_PARAM;
	    TreeView_GetItem(((LPNMHDR) lParam)->hwndFrom, &item);
	    {
		/* Destroy all controls in the currently visible panel. */
		int k;
		HWND item;
		struct winctrl *c;

		while ((c = winctrl_findbyindex(&ctrls_panel, 0)) != NULL) {
		    for (k = 0; k < c->num_ids; k++) {
			item = GetDlgItem(hwnd, c->base_id + k);
			if (item)
			    DestroyWindow(item);
		    }
		    winctrl_rem_shortcuts(&dp, c);
		    winctrl_remove(&ctrls_panel, c);
		    sfree(c->data);
		    sfree(c);
		}
	    }
	    create_controls(hwnd, (char *)item.lParam);

	    dlg_refresh(NULL, &dp);    /* set up control values */
 
	    SendMessage (hwnd, WM_SETREDRAW, true, 0);
 	    InvalidateRect (hwnd, NULL, true);

	    SetFocus(((LPNMHDR) lParam)->hwndFrom);	/* ensure focus stays */
	    return 0;
	}
	break;
      case WM_COMMAND:
      case WM_DRAWITEM:
      default:			       /* also handle drag list msg here */
	/*
	 * Only process WM_COMMAND once the dialog is fully formed.
	 */
	if (GetWindowLongPtr(hwnd, GWLP_USERDATA) == 1) {
	    ret = winctrl_handle_command(&dp, msg, wParam, lParam);
	    if (dp.ended && GetCapture() != hwnd)
		SaneEndDialog(hwnd, dp.endresult ? 1 : 0);
	} else
	    ret = 0;
	return ret;
      case WM_HELP:
	if (!winctrl_context_help(&dp, hwnd,
				 ((LPHELPINFO)lParam)->iCtrlId))
	    MessageBeep(0);
        break;
      case WM_CLOSE:
	quit_help(hwnd);
	SaneEndDialog(hwnd, 0);
	return 0;

	/* Grrr Explorer will maximize Dialogs! */
      case WM_SIZE:
	    if (wParam == SIZE_MAXIMIZED)
	    force_normal(hwnd);
	return 0;

    }
    return 0;
}

void modal_about_box(HWND hwnd)
{
    EnableWindow(hwnd, 0);
#if (defined MOD_PERSO) && (!defined FLJ)
	if( GetPuttyFlag() ) {
		DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProcOrig) ;
	} else {
		DialogBox(hinst, MAKEINTRESOURCE(IDD_KITTYABOUT), hwnd, AboutProc) ;
	}
#else
    DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProc);
#endif
    EnableWindow(hwnd, 1);
    SetActiveWindow(hwnd);
}

void show_help(HWND hwnd)
{
    launch_help(hwnd, NULL);
}

void defuse_showwindow(void)
{
    /*
     * Work around the fact that the app's first call to ShowWindow
     * will ignore the default in favour of the shell-provided
     * setting.
     */
    {
	HWND hwnd;
	hwnd = CreateDialog(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX),
			    NULL, NullDlgProc);
	ShowWindow(hwnd, SW_HIDE);
	SetActiveWindow(hwnd);
	DestroyWindow(hwnd);
    }
}

bool do_config(Conf *conf)
{
    bool ret;
#ifdef MOD_PERSO
	// On cree la session "Default Settings" si elle n'existe pas
	if( GetDefaultSettingsFlag() ) if( (IniFileFlag==SAVEMODE_REG) || SessPathIsInitial() ) { 
		char buffer[1024] ;
		if( !IsThereDefaultSessionFile() ) {
			Conf *defconf = conf_new() ;
			load_settings(NULL,defconf);
			if( !strcmp(FileExtension,"") ) {
				if( save_settings( "Default Settings", defconf ) != NULL ) { 
					do_eventlog( "Can not create Default Settings file" ) ; 
				}	
			} else {
				sprintf(buffer,"Default Settings%s", FileExtension);
				if( save_settings( buffer, defconf ) != NULL ) {
					do_eventlog("Can not create Default Settings file" ) ; 
				}
			}
			conf_free( defconf ) ;
		}
	}
#endif
    ctrlbox = ctrl_new_box();
    setup_config_box(ctrlbox, false, 0, 0);
    win_setup_config_box(ctrlbox, &dp.hwnd, has_help(), false, 0);
    dp_init(&dp);
    winctrl_init(&ctrls_base);
    winctrl_init(&ctrls_panel);
    dp_add_tree(&dp, &ctrls_base);
    dp_add_tree(&dp, &ctrls_panel);
    dp.wintitle = dupprintf("%s Configuration", appname);
    dp.errtitle = dupprintf("%s Error", appname);
    dp.data = conf;
    dlg_auto_set_fixed_pitch_flag(&dp);
    dp.shortcuts['g'] = true;	       /* the treeview: `Cate&gory' */

    ret =
	SaneDialogBox(hinst, MAKEINTRESOURCE(IDD_MAINBOX), NULL,
		  GenericMainDlgProc);

    ctrl_free_box(ctrlbox);
    winctrl_cleanup(&ctrls_panel);
    winctrl_cleanup(&ctrls_base);
    dp_cleanup(&dp);

#ifdef MOD_PERSO
	GotoConfigDirectory() ;
	if( ret==false ) SaveRegistryKey( ) ; // On sort de la config box par ESCAPE ou cancel
	else _beginthread( routine_SaveRegistryKey, 0, (void*)NULL ) ; // On démarre une session
#endif
    return ret;
}

bool do_reconfig(HWND hwnd, Conf *conf, int protcfginfo)
{
    Conf *backup_conf;
    bool ret;
    int protocol;

    backup_conf = conf_copy(conf);

    ctrlbox = ctrl_new_box();
    protocol = conf_get_int(conf, CONF_protocol);
    setup_config_box(ctrlbox, true, protocol, protcfginfo);
    win_setup_config_box(ctrlbox, &dp.hwnd, has_help(), true, protocol);
    dp_init(&dp);
    winctrl_init(&ctrls_base);
    winctrl_init(&ctrls_panel);
    dp_add_tree(&dp, &ctrls_base);
    dp_add_tree(&dp, &ctrls_panel);
    dp.wintitle = dupprintf("%s Reconfiguration", appname);
    dp.errtitle = dupprintf("%s Error", appname);
    dp.data = conf;
    dlg_auto_set_fixed_pitch_flag(&dp);
    dp.shortcuts['g'] = true;	       /* the treeview: `Cate&gory' */

    ret = SaneDialogBox(hinst, MAKEINTRESOURCE(IDD_MAINBOX), NULL,
		  GenericMainDlgProc) ;
		  
    ctrl_free_box(ctrlbox);
    winctrl_cleanup(&ctrls_base);
    winctrl_cleanup(&ctrls_panel);
    dp_cleanup(&dp);

    if (!ret)
	conf_copy_into(conf, backup_conf);
#if (defined MOD_BACKGROUNDIMAGE) && (!defined FLJ)
	if( GetBackgroundImageFlag() && (conf_get_int(conf,CONF_bg_slideshow)!=conf_get_int(backup_conf,CONF_bg_slideshow)) ) {
		KillTimer( hwnd, TIMER_SLIDEBG ) ;
		if((conf_get_int(conf,CONF_bg_type)!=0)&&(conf_get_int(conf,CONF_bg_slideshow)>0)) 
			SetTimer(hwnd, TIMER_SLIDEBG, (int)(conf_get_int(conf,CONF_bg_slideshow)*1000), NULL) ;
		InvalidateRect(hwnd, NULL, TRUE);
	}
#endif

    conf_free(backup_conf);

    return ret;
}

static void win_gui_eventlog(LogPolicy *lp, const char *string)
{
    char timebuf[40];
    char **location;
    struct tm tm;

    tm=ltime();
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S\t", &tm);

    if (ninitial < LOGEVENT_INITIAL_MAX)
        location = &events_initial[ninitial];
    else
        location = &events_circular[(circular_first + ncircular) % LOGEVENT_CIRCULAR_MAX];

    if (*location)
        sfree(*location);
    *location = dupcat(timebuf, string);
    if (logbox) {
	int count;
	SendDlgItemMessage(logbox, IDN_LIST, LB_ADDSTRING,
			   0, (LPARAM) *location);
	count = SendDlgItemMessage(logbox, IDN_LIST, LB_GETCOUNT, 0, 0);
	SendDlgItemMessage(logbox, IDN_LIST, LB_SETTOPINDEX, count - 1, 0);
    }
    if (ninitial < LOGEVENT_INITIAL_MAX) {
        ninitial++;
    } else if (ncircular < LOGEVENT_CIRCULAR_MAX) {
        ncircular++;
    } else if (ncircular == LOGEVENT_CIRCULAR_MAX) {
        circular_first = (circular_first + 1) % LOGEVENT_CIRCULAR_MAX;
        sfree(events_circular[circular_first]);
        events_circular[circular_first] = dupstr("..");
    }
}

static void win_gui_logging_error(LogPolicy *lp, const char *event)
{
    WinGuiSeat *wgs = container_of(lp, WinGuiSeat, logpolicy);

    /* Send 'can't open log file' errors to the terminal window.
     * (Marked as stderr, although terminal.c won't care.) */
    seat_stderr_pl(&wgs->seat, ptrlen_from_asciz(event));
    seat_stderr_pl(&wgs->seat, PTRLEN_LITERAL("\r\n"));
}

void showeventlog(HWND hwnd)
{
    if (!logbox) {
	logbox = CreateDialog(hinst, MAKEINTRESOURCE(IDD_LOGBOX),
			      hwnd, LogProc);
	ShowWindow(logbox, SW_SHOWNORMAL);
    }
    SetActiveWindow(logbox);
}

#if (defined MOD_PERSO) && (!defined FLJ)
void showabout(HWND hwnd)
{
	/*
	char buffer[1024] ;
    DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProc);
	sprintf( buffer, "That's all folks ! version\r\n%s", BuildVersionTime ) ;
    MessageBox( hwnd, buffer, "Info", MB_OK ) ;
	*/
	if( get_param("PUTTY") ) DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProcOrig);
	else {
		DialogBox(hinst, MAKEINTRESOURCE(IDD_KITTYABOUT), hwnd, AboutProc);
		/*if( GetIconeFlag() != -1 ) SetNewIcon( hwnd, conf_get_filename(conf,CONF_iconefile)->path, conf_get_int(conf,CONF_icone), SI_NEXT ) ;
		if( GetIconeFlag() > 0 ) {
			time_t ttime = time( NULL ) % GetNumberOfIcons() ;
			SendMessage( hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon( hinst, MAKEINTRESOURCE(IDI_MAINICON_0 + ttime ) ) );
			}*/
		}
}
#else
void showabout(HWND hwnd)
{
    DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProc);
}
#endif

struct hostkey_dialog_ctx {
    const char *const *keywords;
    const char *const *values;
    FingerprintType fptype_default;
    char **fingerprints;
    const char *keydisp;
    LPCTSTR iconid;
    const char *helpctx;
};

static INT_PTR CALLBACK HostKeyMoreInfoProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        const struct hostkey_dialog_ctx *ctx =
            (const struct hostkey_dialog_ctx *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (INT_PTR)ctx);

        if (ctx->fingerprints[SSH_FPTYPE_SHA256])
            SetDlgItemText(hwnd, IDC_HKI_SHA256,
                           ctx->fingerprints[SSH_FPTYPE_SHA256]);
        if (ctx->fingerprints[SSH_FPTYPE_MD5])
            SetDlgItemText(hwnd, IDC_HKI_MD5,
                           ctx->fingerprints[SSH_FPTYPE_MD5]);

        SetDlgItemText(hwnd, IDA_TEXT, ctx->keydisp);
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
            EndDialog(hwnd, 0);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, 0);
        return 0;
    }
    return 0;
}

static INT_PTR CALLBACK HostKeyDialogProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        strbuf *sb = strbuf_new();
        const struct hostkey_dialog_ctx *ctx =
            (const struct hostkey_dialog_ctx *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (INT_PTR)ctx);
        for (int id = 100;; id++) {
            char buf[256];

            if (!GetDlgItemText(hwnd, id, buf, (int)lenof(buf)))
                break;

            strbuf_clear(sb);
            for (const char *p = buf; *p ;) {
                if (*p == '{') {
                    for (size_t i = 0; ctx->keywords[i]; i++) {
                        if (strstartswith(p, ctx->keywords[i])) {
                            p += strlen(ctx->keywords[i]);
                            put_datapl(sb, ptrlen_from_asciz(ctx->values[i]));
                            goto matched;
                        }
                    }
                } else {
                    put_byte(sb, *p++);
                }
              matched:;
            }

            SetDlgItemText(hwnd, id, sb->s);
        }
        strbuf_free(sb);

        SetDlgItemText(hwnd, IDC_HK_FINGERPRINT,
                       ctx->fingerprints[ctx->fptype_default]);
        MakeDlgItemBorderless(hwnd, IDC_HK_FINGERPRINT);

        HANDLE icon = LoadImage(
            NULL, ctx->iconid, IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
            LR_SHARED);
        SendDlgItemMessage(hwnd, IDC_HK_ICON, STM_SETICON, (WPARAM)icon, 0);

        if (!has_help()) {
            HWND item = GetDlgItem(hwnd, IDHELP);
            if (item)
                DestroyWindow(item);
        }

#ifdef MOD_PERSO
	    SaveRegistryKey() ;
#endif
	    return 1;
      }
      case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND control = (HWND)lParam;

        if (GetWindowLongPtr(control, GWLP_ID) == IDC_HK_TITLE) {
            SetBkMode(hdc, TRANSPARENT);
            HFONT prev_font = (HFONT)SelectObject(
                hdc, (HFONT)GetStockObject(SYSTEM_FONT));
            LOGFONT lf;
            if (GetObject(prev_font, sizeof(lf), &lf)) { 
                lf.lfWeight = FW_BOLD;
                lf.lfHeight = lf.lfHeight * 3 / 2;
                HFONT bold_font = CreateFontIndirect(&lf);
                if (bold_font)
                    SelectObject(hdc, bold_font);
            }
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }
        return 0;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDC_HK_ACCEPT:
          case IDC_HK_ONCE:
          case IDCANCEL:
            EndDialog(hwnd, LOWORD(wParam));
            return 0;
          case IDHELP: {
            const struct hostkey_dialog_ctx *ctx =
                (const struct hostkey_dialog_ctx *)
                GetWindowLongPtr(hwnd, GWLP_USERDATA);
            launch_help(hwnd, ctx->helpctx);
            return 0;
          }
          case IDC_HK_MOREINFO: {
            const struct hostkey_dialog_ctx *ctx =
                (const struct hostkey_dialog_ctx *)
                GetWindowLongPtr(hwnd, GWLP_USERDATA);
            DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_HK_MOREINFO),
                           hwnd, HostKeyMoreInfoProc, (LPARAM)ctx);
          }
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return 0;
    }
    return 0;
}

int win_seat_verify_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, const char *keydisp, char **fingerprints,
    void (*callback)(void *ctx, int result), void *ctx)
{
    int ret;

    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);

    /*
     * Verify the key against the registry.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)                      /* success - key matched OK */
        return 1;
    else {
        static const char *const keywords[] =
            { "{KEYTYPE}", "{APPNAME}", NULL };

        const char *values[2];
        values[0] = keytype;
        values[1] = appname;

        struct hostkey_dialog_ctx ctx[1];
        ctx->keywords = keywords;
        ctx->values = values;
        ctx->fingerprints = fingerprints;
        ctx->fptype_default = ssh2_pick_default_fingerprint(fingerprints);
        ctx->keydisp = keydisp;
        ctx->iconid = (ret == 2 ? IDI_WARNING : IDI_QUESTION);
        ctx->helpctx = (ret == 2 ? WINHELP_CTX_errors_hostkey_changed :
                        WINHELP_CTX_errors_hostkey_absent);
        int dlgid = (ret == 2 ? IDD_HK_WRONG : IDD_HK_ABSENT);
#ifdef MOD_PERSO
        int mbret ;
	if( GetAutoStoreSSHKeyFlag() ) { 
	    do_eventlog("Auto update host key") ;
	    mbret=IDC_HK_ACCEPT ; 
	} else
	    mbret = DialogBoxParam(
            hinst, MAKEINTRESOURCE(dlgid), wgs->term_hwnd,
            HostKeyDialogProc, (LPARAM)ctx);
#else
        int mbret = DialogBoxParam(
            hinst, MAKEINTRESOURCE(dlgid), wgs->term_hwnd,
            HostKeyDialogProc, (LPARAM)ctx);
#endif
        assert(mbret==IDC_HK_ACCEPT || mbret==IDC_HK_ONCE || mbret==IDCANCEL);
        if (mbret == IDC_HK_ACCEPT) {
            store_host_key(host, port, keytype, keystr);
            return 1;
        } else if (mbret == IDC_HK_ONCE)
	    return 1;
    }
    return 0;	/* abandon the connection */
}

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
int win_seat_confirm_weak_crypto_primitive(
    Seat *seat, const char *algtype, const char *algname,
    void (*callback)(void *ctx, int result), void *ctx)
{
    static const char mbtitle[] = "%s Security Alert";
    static const char msg[] =
	"The first %s supported by the server\n"
	"is %s, which is below the configured\n"
	"warning threshold.\n"
	"Do you want to continue with this connection?\n";
    char *message, *title;
    int mbret;

    message = dupprintf(msg, algtype, algname);
    title = dupprintf(mbtitle, appname);
    mbret = MessageBox(NULL, message, title,
		       MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    socket_reselect_all();
    sfree(message);
    sfree(title);
    if (mbret == IDYES)
	return 1;
    else
	return 0;
}

int win_seat_confirm_weak_cached_hostkey(
    Seat *seat, const char *algname, const char *betteralgs,
    void (*callback)(void *ctx, int result), void *ctx)
{
    static const char mbtitle[] = "%s Security Alert";
    static const char msg[] =
	"The first host key type we have stored for this server\n"
	"is %s, which is below the configured warning threshold.\n"
	"The server also provides the following types of host key\n"
        "above the threshold, which we do not have stored:\n"
        "%s\n"
	"Do you want to continue with this connection?\n";
    char *message, *title;
    int mbret;

    message = dupprintf(msg, algname, betteralgs);
    title = dupprintf(mbtitle, appname);
    mbret = MessageBox(NULL, message, title,
		       MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    socket_reselect_all();
    sfree(message);
    sfree(title);
    if (mbret == IDYES)
	return 1;
    else
	return 0;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
static int win_gui_askappend(LogPolicy *lp, Filename *filename,
                             void (*callback)(void *ctx, int result),
                             void *ctx)
{
    static const char msgtemplate[] =
	"The session log file \"%.*s\" already exists.\n"
	"You can overwrite it with a new session log,\n"
	"append your session log to the end of it,\n"
	"or disable session logging for this session.\n"
	"Hit Yes to wipe the file, No to append to it,\n"
	"or Cancel to disable logging.";
    char *message;
    char *mbtitle;
    int mbret;

    message = dupprintf(msgtemplate, FILENAME_MAX, filename->path);
    mbtitle = dupprintf("%s Log to File", appname);

    mbret = MessageBox(NULL, message, mbtitle,
		       MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON3);

    socket_reselect_all();

    sfree(message);
    sfree(mbtitle);

    if (mbret == IDYES)
	return 2;
    else if (mbret == IDNO)
	return 1;
    else
	return 0;
}

const LogPolicyVtable win_gui_logpolicy_vt = {
    .eventlog = win_gui_eventlog,
    .askappend = win_gui_askappend,
    .logging_error = win_gui_logging_error,
    .verbose = null_lp_verbose_yes,
};

/*
 * Warn about the obsolescent key file format.
 * 
 * Uniquely among these functions, this one does _not_ expect a
 * frontend handle. This means that if PuTTY is ported to a
 * platform which requires frontend handles, this function will be
 * an anomaly. Fortunately, the problem it addresses will not have
 * been present on that platform, so it can plausibly be
 * implemented as an empty function.
 */
void old_keyfile_warning(void)
{
    static const char mbtitle[] = "%s Key File Warning";
    static const char message[] =
	"You are loading an SSH-2 private key which has an\n"
	"old version of the file format. This means your key\n"
	"file is not fully tamperproof. Future versions of\n"
	"%s may stop supporting this private key format,\n"
	"so we recommend you convert your key to the new\n"
	"format.\n"
	"\n"
	"You can perform this conversion by loading the key\n"
	"into PuTTYgen and then saving it again.";

    char *msg, *title;
    msg = dupprintf(message, appname);
    title = dupprintf(mbtitle, appname);

    MessageBox(NULL, msg, title, MB_OK);

    socket_reselect_all();

    sfree(msg);
    sfree(title);
}
