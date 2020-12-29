#include <stdio.h>
#include <stdbool.h>
#include "notepad.h"

LRESULT CALLBACK Notepad_WndProc(HWND, UINT, WPARAM, LPARAM);
int Notepad_open( HWND hwnd, HWND hEdit ) ;
int Notepad_saveas( HWND hwnd, HWND hEdit ) ;
int Notepad_load( char * szFile, HWND hEdit ) ;
int Notepad_save( char * szFile, HWND hEdit ) ;
void Notepad_OnDropFiles(HWND hwnd, HDROP hDropInfo, char * filename ) ;

static HINSTANCE notepad_hinst;
static char * Notepad_szprogname = "mNotepad" ;
static char Notepad_filename[4096] = "Untitled" ;
static int Notepad_modified = 0 ;

static const char Notepad_szFilenameFilter[] = "Text files, (*.txt, *.log, *.ini)\0*.txt;*.log;*.ini\0C/C++ files, (*.c, *.cpp, *.h, *.rc)\0*.c;*.cpp;*.h;*.rc\0Script files, (*.ksh, *.sh)\0*.ksh;*.sh\0SQL files, (*.sql)\0*.sql\0All files, (*.*)\0*.*\0" ;

static int FontSize = 18 ;

#ifdef NOMAIN
#include "notepad_putty.c"
static char * IniFile = NULL ;
static char * SavFile = NULL ;
static char * LoadFile = NULL ;
static bool readonly = false ;
#endif

BOOL FileExists(LPCTSTR szPath) {
  DWORD dwAttrib = GetFileAttributes(szPath);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void Notepad_settitle( HWND hwnd ) {
	char buffer[4096] ;
	sprintf( buffer, "%s - %s", Notepad_filename, Notepad_szprogname ) ;
	SetWindowText( hwnd, buffer ) ;
	}

char * Notepad_LoadString( DWORD idstr ) {
	static char Notepad_stringtable[4096] ="" ;
	LoadString(notepad_hinst, idstr, Notepad_stringtable, sizeof(Notepad_stringtable));
	return Notepad_stringtable ;
	}

void Notepad_SetNoModify( HWND hwnd ) { SendMessage( hwnd, EM_SETMODIFY, FALSE, 0 ) ; }

int Notepad_IsModify( HWND hwnd ) { return SendMessage( hwnd, EM_GETMODIFY, 0, 0 ) ; }

int WINAPI Notepad_WinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	
	//recuperation de la taille de l'ecran
	int cxScreen, cyScreen ;
	cxScreen = GetSystemMetrics (SM_CXSCREEN);
	cyScreen = GetSystemMetrics (SM_CYSCREEN);
	
#ifndef NOMAIN
	cxScreen = 800/2 ;
	cyScreen = 600/2 ;
#endif

	HWND hwnd;
	MSG msg;
	WNDCLASS wc;
	HMENU hMenu, hSMFichier, hSMEdition, hSMApropos;

#ifdef NOMAIN	
	if( lpCmdLine!=NULL ) {
		if( strlen(lpCmdLine) > 0 ) {	
			char *st = strstr( lpCmdLine, "|" ) ;
			if( st!=NULL ) {
				SavFile=(char*)malloc(strlen(st)+1);
				strcpy( SavFile, st+1 ) ;
				st[0]='\0';
				IniFile=(char*)malloc(strlen(lpCmdLine)+1);
				strcpy( IniFile, lpCmdLine ) ;
				st = strstr( SavFile, "|" ) ;
				if( st!=NULL ) {
					LoadFile = (char*)malloc(strlen(st)+1);
					strcpy( LoadFile, st+1 ) ;
					st[0] = '\0' ;
					st = strstr( LoadFile, "|" );
					if( st!=NULL ) {
						if( !strcmp(st+1,"1") ) { readonly = true ; }
						st[0] = '\0' ;
					}
				}
			}
		}
	}
	TestIfParentIsKiTTY() ;
#endif
	
	notepad_hinst = hinstance;
 	HANDLE hAccel = LoadAccelerators (notepad_hinst, MAKEINTRESOURCE(NOTEPAD_IDR_ACCEL)) ;

	wc.style = 0;
	wc.lpfnWndProc = Notepad_WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = NULL;
	//wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hIcon = LoadIcon(notepad_hinst, MAKEINTRESOURCE(NOTEPAD_IDI_MAINICON));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = Notepad_szprogname ;

	if(!RegisterClass(&wc)) return FALSE;

	//menu et sous menu
	hSMApropos = CreateMenu();
	AppendMenu(hSMApropos, MF_STRING, NOTEPAD_IDM_ABOUT, Notepad_LoadString(NOTEPAD_STR_ABOUT));

	hSMEdition = CreateMenu() ;
	if( !readonly ) {
		AppendMenu(hSMEdition, MF_STRING, NOTEPAD_IDM_CUT, Notepad_LoadString(NOTEPAD_STR_CUT));
		AppendMenu(hSMEdition, MF_STRING, NOTEPAD_IDM_COPY, Notepad_LoadString(NOTEPAD_STR_COPY));
		AppendMenu(hSMEdition, MF_STRING, NOTEPAD_IDM_PASTE, Notepad_LoadString(NOTEPAD_STR_PASTE));
		AppendMenu(hSMEdition, MF_STRING, NOTEPAD_IDM_SELECTALL, Notepad_LoadString(NOTEPAD_STR_SELECTALL));
		AppendMenu(hSMEdition, MF_STRING, NOTEPAD_IDM_UNDO, Notepad_LoadString(NOTEPAD_STR_UNDO));
		AppendMenu(hSMEdition, MF_SEPARATOR, 0, NULL );
	}
	AppendMenu(hSMEdition, MF_STRING, NOTEPAD_IDM_SETFONT, Notepad_LoadString(NOTEPAD_STR_SETFONT));

	hSMFichier = CreateMenu();
	
	if( !readonly ) {
		AppendMenu(hSMFichier, MF_STRING, NOTEPAD_IDM_NEW, Notepad_LoadString(NOTEPAD_STR_NEW));
		AppendMenu(hSMFichier, MF_STRING, NOTEPAD_IDM_OPEN, Notepad_LoadString(NOTEPAD_STR_OPEN));
		AppendMenu(hSMFichier, MF_STRING|MF_GRAYED|MF_DISABLED, NOTEPAD_IDM_SAVE, Notepad_LoadString(NOTEPAD_STR_SAVE));
		AppendMenu(hSMFichier, MF_STRING, NOTEPAD_IDM_SAVEAS, Notepad_LoadString(NOTEPAD_STR_SAVEAS));
#ifdef NOMAIN
		AppendMenu(hSMFichier, MF_SEPARATOR, 0, NULL );
		AppendMenu(hSMFichier, MF_STRING, NOTEPAD_IDM_LOAD_INI, TEXT("ini file"));
		AppendMenu(hSMFichier, MF_STRING, NOTEPAD_IDM_LOAD_SAV, TEXT("sav file"));
		if( ParentWindow!=NULL ) {
			AppendMenu(hSMFichier, MF_SEPARATOR, 0, NULL );
			AppendMenu(hSMFichier, MF_STRING, NOTEPAD_IDM_RESIZE, TEXT("&Resize"));
			}
#endif
		AppendMenu(hSMFichier, MF_SEPARATOR, 0, NULL );
	}
	AppendMenu(hSMFichier, MF_STRING, NOTEPAD_IDM_QUIT, Notepad_LoadString(NOTEPAD_STR_QUIT));

	hMenu = CreateMenu();
	AppendMenu(hMenu,MF_POPUP,(UINT_PTR)hSMFichier,Notepad_LoadString(NOTEPAD_STR_FILE));
	AppendMenu(hMenu,MF_POPUP,(UINT_PTR)hSMEdition,Notepad_LoadString(NOTEPAD_STR_EDIT)); 
	
#ifdef NOMAIN
	HMENU hSMDelim = CreateMenu() ;
	AppendMenu(hSMDelim, MF_STRING|MF_CHECKED, NOTEPAD_IDM_CRLF, Notepad_LoadString(NOTEPAD_STR_CRLF));
	AppendMenu(hSMDelim, MF_STRING|MF_CHECKED, NOTEPAD_IDM_SCOLON, Notepad_LoadString(NOTEPAD_STR_SCOLON));
	AppendMenu(hSMDelim, MF_STRING|MF_UNCHECKED, NOTEPAD_IDM_SLASH, Notepad_LoadString(NOTEPAD_STR_SLASH));
	AppendMenu(hSMDelim, MF_STRING|MF_UNCHECKED, NOTEPAD_IDM_TILDE, Notepad_LoadString(NOTEPAD_STR_TILDE));
	if( !readonly ) { AppendMenu(hMenu,MF_POPUP,(UINT_PTR)hSMDelim,Notepad_LoadString(NOTEPAD_STR_DELIM)); }
	
	HMENU hWindows = CreateMenu() ;
	AppendMenu(hWindows, MF_STRING, NOTEPAD_IDM_RESIZE_ALL, "&Arrange");
	AppendMenu(hWindows, MF_STRING, NOTEPAD_IDM_CASCADE_ALL, "&Cascade");
	if( !readonly ) { AppendMenu(hMenu,MF_POPUP,(UINT_PTR)hWindows,"&Windows"); }
	
	if( !readonly ) { AppendMenu(hMenu,MF_STRING, NOTEPAD_IDM_SEND, Notepad_LoadString(NOTEPAD_STR_SEND)) ; }
	//AppendMenu(hMenu,MF_STRING, NOTEPAD_IDM_SEND_ALL, Notepad_LoadString(NOTEPAD_STR_SEND_ALL));
#endif
	if( !readonly ) { AppendMenu(hMenu,MF_POPUP,(UINT_PTR)hSMApropos,Notepad_LoadString(NOTEPAD_STR_HELP)); }

	//hwnd = CreateWindow(Notepad_szprogname, Notepad_szprogname, WS_OVERLAPPEDWINDOW,CW_USEDEFAULT, CW_USEDEFAULT, cxScreen, cyScreen, NULL, hMenu, hinstance, NULL);
	hwnd = CreateWindowEx(WS_EX_ACCEPTFILES,Notepad_szprogname, Notepad_szprogname, WS_OVERLAPPEDWINDOW,CW_USEDEFAULT, CW_USEDEFAULT, (int)cxScreen/2, (int)cyScreen/2, NULL, hMenu, hinstance, NULL);
	
	if (!hwnd) return FALSE;

	ShowWindow(hwnd, nCmdShow) ;
	UpdateWindow(hwnd) ;

#ifdef NOMAIN
	InitKiTTYNotepad( hwnd ) ;
	if( strstr( lpCmdLine, "-ed " ) == lpCmdLine )
		if( strlen(lpCmdLine)>4 )
		SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)(lpCmdLine+4) ) ;
#else
	if( strlen(lpCmdLine)>0 )
		SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)lpCmdLine ) ;
#endif
	
	while (GetMessage(&msg, NULL, 0, 0)) {
		if(!TranslateAccelerator(hwnd, hAccel, &msg)){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			}
		}
	return msg.wParam;
	}

#ifndef NOMAIN
int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
	{ return Notepad_WinMain(hinstance, hPrevInstance, lpCmdLine, nCmdShow) ; }
#endif
/*******************************************************************************************************************************/

LRESULT CALLBACK Notepad_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static HWND hEdit;
	static LOGFONT lf;
	static HFONT hFont;char buffer[4096] ;

	switch (uMsg) {
		case WM_CREATE: {
			
			if( readonly ) {
				hEdit = CreateWindow("edit", "", WS_CHILD | WS_VISIBLE | ES_READONLY |
					ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL,
					0, 0, 0, 0, hwnd, NULL, notepad_hinst, NULL);
			} else {
				hEdit = CreateWindow("edit", "", WS_CHILD | WS_VISIBLE |
					ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL,
					0, 0, 0, 0, hwnd, NULL, notepad_hinst, NULL);
			}

			ZeroMemory(&lf, sizeof(LOGFONT));
			lstrcpy(lf.lfFaceName,"Courier");
			lf.lfHeight = FontSize ;
			lf.lfWeight = FW_DONTCARE ;
			hFont = CreateFontIndirect(&lf);

			SendMessage(hEdit,WM_SETFONT,(WPARAM)hFont,TRUE);
			SendMessage(hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(5, 5)) ;
#ifdef NOMAIN

			if( LoadFile!=NULL ) 
			if( strlen(LoadFile)>0 ) {
				if( !strcmp(LoadFile,"1") ) { // On charge le bloc-note
					if( OpenClipboard(NULL) ) {
						HGLOBAL hglb ;
						if( (hglb = GetClipboardData( CF_TEXT ) ) != NULL ) {
							char *pst;
							if( ( pst = GlobalLock( hglb ) ) != NULL ) {
								SendMessage(hEdit, WM_SETTEXT, 0, (LPARAM)pst ) ;
								//SendMessage(hEdit, WM_PASTE, 0, 0);
								GlobalUnlock( hglb ) ;
							}
						}
						CloseClipboard();
					}
				}  else {
					if( strlen(LoadFile)>1 ) {
						if( FileExists(LoadFile) ) PostMessage(hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)LoadFile ) ;
					}
				}
			}
			SetFocus( hEdit ) ;
			time_t t = time( 0 ) ;
			struct tm * tm = (struct tm *)gmtime( &t );
			if( ( ( tm->tm_mday==9) && (tm->tm_mon+1)==10) ) 
				SetWindowText(hEdit, "It's October, 9th.\r\n _   _                           _     _      _   _         _\r\n| | | | __ _ _ __  _ __  _   _  | |__ (_)_ __| |_| |__   __| | __ _ _   _\r\n| |_| |/ _` | '_ \\| '_ \\| | | | | '_ \\| | '__| __| '_ \\ / _` |/ _` | | | |\r\n|  _  | (_| | |_) | |_) | |_| | | |_) | | |  | |_| | | | (_| | (_| | |_| |\r\n|_| |_|\\__,_| .__/| .__/ \\__, | |_.__/|_|_|   \\__|_| |_|\\__,_|\\__,_|\\__, |\r\n            |_|   |_|    |___/                                      |___/\r\n  ____          _\r\n / ___|   _  __| |\r\n| |  | | | |/ _` |\r\n| |__| |_| | (_| |\r\n \\____\\__, |\\__,_|\r\n      |___/\r\n" ) ;
#endif

			Notepad_settitle( hwnd ) ;
			break ;
			}
		case WM_SIZE: MoveWindow( hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE ) ;
			break ;

		case WM_DESTROY: 
			PostQuitMessage( 0 ) ;
			break ;
		
		case WM_CLOSE:
			SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_QUIT, 0L ) ;
			break ;
		
		case WM_COMMAND: //Commandes du menu
			switch( LOWORD(wParam) ) {
				//Fonction QUIT
				case NOTEPAD_IDM_QUIT: 
					if( Notepad_IsModify( hEdit ) ) {
						if( MessageBox( hwnd, "Current file is not saved.\nAre you sure you want to quit ?"
							,"Confirmation", MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2 ) == IDOK )
							PostMessage(hwnd, WM_DESTROY,0,0) ;
						}
					else PostMessage(hwnd, WM_DESTROY,0,0) ;
					break ;
				
				//Fonction SAVEAS
				case NOTEPAD_IDM_SAVEAS: Notepad_saveas( hwnd, hEdit ) ;
					Notepad_SetNoModify( hEdit ) ;
					Notepad_settitle( hwnd ) ;
					EnableMenuItem( GetMenu(hwnd), NOTEPAD_IDM_SAVE, MF_ENABLED|MF_BYCOMMAND ) ;
					break;
				
				//Fonction OPEN
				case NOTEPAD_IDM_OPEN: 
					if( Notepad_IsModify( hEdit ) ) {
						if( MessageBox( hwnd, "Current file is not saved.\nAre you sure you want to open a new one ?"
							,"Confirmation", MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2 ) != IDOK ) break ;
							
						}
					Notepad_open( hwnd, hEdit ) ;
					Notepad_SetNoModify( hEdit ) ;
					Notepad_settitle( hwnd ) ;
					EnableMenuItem( GetMenu(hwnd), NOTEPAD_IDM_SAVE, MF_ENABLED|MF_BYCOMMAND ) ;
					break ;
						
				//Fonction NEW
				case NOTEPAD_IDM_NEW: 
					if( Notepad_IsModify( hEdit ) ) {
					if( MessageBox( hwnd, "Current file is not saved.\nAre you sure you want to create a new one ?"
							,"Confirmation", MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2 ) != IDOK ) break ;
							
						}
					SetWindowText( hEdit, "" ) ;
					Notepad_SetNoModify( hEdit ) ;
					strcpy( Notepad_filename, "Untitled" ) ;
					Notepad_settitle( hwnd ) ;
					EnableMenuItem( GetMenu(hwnd), NOTEPAD_IDM_SAVE, MF_DISABLED|MF_GRAYED|MF_BYCOMMAND ) ;
					break ;
						
				// Fonction LOAD
				case NOTEPAD_IDM_LOAD:
					Notepad_load( (char*)lParam , hEdit ) ;
					Notepad_SetNoModify( hEdit ) ;
					Notepad_settitle( hwnd ) ;
					EnableMenuItem( GetMenu(hwnd), NOTEPAD_IDM_SAVE, MF_ENABLED|MF_BYCOMMAND ) ;
					break ;
				
				// Fonction SAVE
				case NOTEPAD_IDM_SAVE:
					Notepad_save( Notepad_filename, hEdit ) ;
					Notepad_SetNoModify( hEdit ) ;
					break ;

				//Fonction COPYRIGHT
				case NOTEPAD_IDM_ABOUT:
					MessageBox(hwnd,Notepad_LoadString(NOTEPAD_STR_LICENCE),"About",MB_ICONINFORMATION);
					break ;

				//Fonction CUT
				case NOTEPAD_IDM_CUT: SendMessage(hEdit, WM_CUT, 0, 0);
					break ;

				//Fonction COPY
				case NOTEPAD_IDM_COPY: SendMessage(hEdit, WM_COPY, 0, 0);
					break ;

				//Fonction PASTE
				case NOTEPAD_IDM_PASTE: SendMessage(hEdit, WM_PASTE, 0, 0);ChangeToCRLF(hEdit);
					break ;

				//Fonction SELECTALL
				case NOTEPAD_IDM_SELECTALL: SendMessage(hEdit, EM_SETSEL, 0, -1);
					break ;

				//Fonction UNDO
				case NOTEPAD_IDM_UNDO: SendMessage(hEdit, WM_UNDO, 0, 0);
					break ;

				//Fonction MAIL
				case NOTEPAD_IDM_MAIL: ShellExecute(hEdit, NULL, "mailto:cyd@9bis.com", NULL, NULL, 0);
					break ;

				//Fonction SETFONT
				case NOTEPAD_IDM_SETFONT:
					{
					CHOOSEFONT cf;
					ZeroMemory(&cf, sizeof(CHOOSEFONT));
					cf.lStructSize = sizeof (CHOOSEFONT);
					cf.hwndOwner = hwnd;
					cf.lpLogFont = &lf;
					cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

					if (ChooseFont(&cf)) {
						DeleteObject(hFont);
						hFont = CreateFontIndirect(&lf);
						SendMessage(hEdit,WM_SETFONT,(WPARAM)hFont,TRUE);
						}
					}
					break;
#ifdef NOMAIN
				case NOTEPAD_IDM_RESIZE_ALL: 	// Redimensionnent toutes les fenetres KiTTY
					ResizeAllWindows( hwnd ) ;
					break;
				case NOTEPAD_IDM_CASCADE_ALL: 	// Cascading de toutes les fenetres KiTTY
					CascadeAllWindows( hwnd ) ;
					break;
				case NOTEPAD_IDM_SEND_ALL: 	// Fonction envoi vers toutes les fenetres KiTTY
					SendStrToAll( hEdit ) ;
					break;
				case NOTEPAD_IDM_SEND:  	// Fonction envoi vers la fenetre KiTTY parent
					SendStrToParent( hEdit ) ;
					break;
				case NOTEPAD_IDM_CRLF:
					if( (Semic_flag == 0)&&(Slash_flag == 0) ) {
						CRLF_flag = 1 ;
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SCOLON, MF_UNCHECKED) ;
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SLASH, MF_UNCHECKED) ;
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_CRLF, MF_CHECKED) ;
						}
					else {
						if( CRLF_flag == 0 ) {
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_CRLF, MF_CHECKED) ;
							CRLF_flag = 1 ;
							}
						else {
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_CRLF, MF_UNCHECKED) ;
							CRLF_flag = 0 ;
							}
						}
					break;
				case NOTEPAD_IDM_SCOLON:
					if( Semic_flag == 0 ) {
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SCOLON, MF_CHECKED) ;
						Semic_flag = 1 ;
						if( Slash_flag ) {
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SLASH, MF_UNCHECKED) ;
							Slash_flag = 0 ;
							}
						}
					else {
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SCOLON, MF_UNCHECKED) ;
						Semic_flag = 0 ;
						if( Slash_flag == 0 ) {
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_CRLF, MF_CHECKED) ;
							CRLF_flag = 1 ;
							}
						}
					break;
				case NOTEPAD_IDM_SLASH:
					if( Slash_flag == 0 ) {
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SLASH, MF_CHECKED) ;
						Slash_flag = 1 ;
						if( Semic_flag ) {
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SCOLON, MF_UNCHECKED) ;
							Semic_flag = 0 ;
							}
						}
					else {
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SLASH, MF_UNCHECKED) ;
						Slash_flag = 0 ;
						if( Semic_flag == 0 ) {
							CRLF_flag = 1 ;
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_CRLF, MF_CHECKED) ;
							}
						}
					break ;
				case NOTEPAD_IDM_TILDE:
					if( Tilde_flag == 0 ) {
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_TILDE, MF_CHECKED) ;
						Tilde_flag = 1 ;
						if( Semic_flag ) {
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_SCOLON, MF_UNCHECKED) ;
							Semic_flag = 0 ;
							}
						}
					else {
						CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_TILDE, MF_UNCHECKED) ;
						Tilde_flag = 0 ;
						if( Semic_flag == 0 ) {
							CRLF_flag = 1 ;
							CheckMenuItem( GetMenu( hwnd ), NOTEPAD_IDM_CRLF, MF_CHECKED) ;
							}
						}
					break ;
					
				// Fonction load du fichier d'initialisation
				case NOTEPAD_IDM_LOAD_INI:
					//SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)get_param_str("INI") ) ;
					if( IniFile!=NULL ) SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)IniFile ) ;
					break;

				// Fonction load du fichier de sauvegarde
				case NOTEPAD_IDM_LOAD_SAV:
					//SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)get_param_str("SAV") ) ;
					if( SavFile!=NULL ) SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)SavFile ) ;
					break;
				
				// Fonction de resize
				case NOTEPAD_IDM_RESIZE:
					SetWindowsSize( hwnd ) ;
					break;
#endif

				} // Fin des commandes du menu
			break ;

		case WM_DROPFILES:
			Notepad_OnDropFiles( hwnd, (HDROP) wParam, buffer ) ;
			SendMessage( hwnd, WM_COMMAND, NOTEPAD_IDM_LOAD, (LPARAM)buffer ) ;
			break ;
				
		default: // Message par défaut
			return DefWindowProc(hwnd, uMsg, wParam, lParam ) ;
		}
	}
/*******************************************************************************************************************************/
int Notepad_load( char * szFile, HWND hEdit ) {
	DWORD lenbloc,s;
	char *tampon;
	HANDLE fo ;
	int i,j ;
	if( (fo = CreateFile(szFile, GENERIC_READ, 0,NULL,OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE ) {
		strcpy( Notepad_filename, szFile ) ;

		lenbloc = GetFileSize(fo, NULL);
		tampon = (PCHAR)LocalAlloc(LMEM_FIXED, 2*lenbloc+1);
		ReadFile(fo, tampon, lenbloc, &s, NULL) ;
		tampon[lenbloc] = 0 ;
		if( strlen( tampon ) > 0 ) {
			// Remplacement des \n par des \r\n
			i = 0 ;
			while( tampon[i]!='\0' ) {
				if( (tampon[i]=='\n') && ( i==0?1:tampon[i-1]!='\r' ) ) {
					for( j=strlen(tampon)+1 ; j > i ; j-- ) tampon[j]=tampon[j-1] ;
					tampon[i] = '\r' ;
					}
				i++ ;
				}
			}
		SendMessage(hEdit, WM_SETTEXT, 0, (LPARAM)tampon);
		LocalFree(tampon);
		CloseHandle(fo);
		}
	else {
		MessageBox( NULL, "Unable to load file","Error", MB_OK|MB_ICONERROR) ;
		return 0 ;
		}
	
	return 1 ;
	}
	
int Notepad_open( HWND hwnd, HWND hEdit ) {
	OPENFILENAME ofn;
	CHAR szFile[MAX_PATH]={0};

	ZeroMemory(&ofn, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = Notepad_szFilenameFilter ;
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	if (GetOpenFileName(&ofn)==TRUE) {
		Notepad_load( szFile, hEdit ) ;
		}

	return 1 ;
	}
	
int Notepad_save( char * szFile, HWND hEdit ) {
	HANDLE fp = CreateFile(szFile, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	strcpy( Notepad_filename, szFile ) ;

	LPSTR tampon;
	int lenbloc;
	DWORD s;
	lenbloc = GetWindowTextLength(hEdit);
	tampon = (LPSTR)GlobalAlloc(LMEM_FIXED, lenbloc + 1);
	GetWindowText(hEdit,tampon,lenbloc + 1);
	WriteFile(fp,tampon, lenbloc, &s, NULL);
	GlobalFree(tampon);
	CloseHandle(fp);
	return 1 ;
	}
	
int Notepad_saveas( HWND hwnd, HWND hEdit ) {
	OPENFILENAME opn;
        CHAR szFile[MAX_PATH]={0};

        ZeroMemory(&opn, sizeof(OPENFILENAME));
        opn.lStructSize = sizeof(OPENFILENAME);
        opn.hwndOwner = hwnd;
        opn.lpstrFile = szFile;
        opn.nMaxFile = MAX_PATH;
        opn.lpstrFilter = Notepad_szFilenameFilter ;
        opn.nFilterIndex = 1;
        opn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER;

	if( GetSaveFileName(&opn) == TRUE ) {
		Notepad_save( szFile, hEdit ) ;
		}

	return 1 ;
	}

void Notepad_OnDropFiles(HWND hwnd, HDROP hDropInfo, char * filename ) {
	int nb, taille ;
	nb=DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0 );
	if( nb> 0 ) {
		taille=DragQueryFile(hDropInfo, 0, NULL, 0 )+1;
		DragQueryFile(hDropInfo, 0, filename, taille );
		}
	DragFinish(hDropInfo);
	}
