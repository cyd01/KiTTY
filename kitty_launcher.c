#ifdef MOD_LAUNCHER

// define recupere de WIN_RES.H
#ifndef IDI_PUTTY_LAUNCH
#define IDI_PUTTY_LAUNCH 9901
#endif
#ifndef IDI_BLACKBALL
#define IDI_BLACKBALL 9902
#endif

#define KLWM_NOTIFYICON		(WM_USER+2)
static HMENU MenuLauncher = NULL ;
static HMENU HideMenu ;
static int LauncherConfReload = 1 ;
static HBITMAP bmpCheck, bmpUnCheck ;

// Gestion Hide/UnHide all
static struct THWin { HWND hwnd ; char name[128] ; } TabWin[100] ;
static int oldIconFlag = 0 ;
static int NbWin = 0 ;
static int IsUnique = 0 ;
int RefreshWinList( HWND hwnd ) ;

#ifndef OBM_CHECKBOXES
#define OBM_CHECKBOXES 32759
#endif

// Creation de bitmap coche
HBITMAP GetMyCheckBitmaps(UINT fuCheck) 
{ 
    COLORREF crBackground;  // background color                  
    HBRUSH hbrBackground;   // background brush                  
    HBRUSH hbrTargetOld;    // original background brush         
    HDC hdcSource;          // source device context             
    HDC hdcTarget;          // target device context             
    HBITMAP hbmpCheckboxes; // handle to check-box bitmap        
    BITMAP bmCheckbox;      // structure for bitmap data         
    HBITMAP hbmpSourceOld;  // handle to original source bitmap  
    HBITMAP hbmpTargetOld;  // handle to original target bitmap  
    HBITMAP hbmpCheck;      // handle to check-mark bitmap       
    RECT rc;                // rectangle for check-box bitmap    
    WORD wBitmapX;          // width of check-mark bitmap        
    WORD wBitmapY;          // height of check-mark bitmap       
 
    // Get the menu background color and create a solid brush 
    // with that color. 
 
    crBackground = GetSysColor(COLOR_MENU); 
    hbrBackground = CreateSolidBrush(crBackground); 
 
    // Create memory device contexts for the source and 
    // destination bitmaps. 
 
    hdcSource = CreateCompatibleDC((HDC) NULL); 
    hdcTarget = CreateCompatibleDC(hdcSource); 
 
    // Get the size of the system default check-mark bitmap and 
    // create a compatible bitmap of the same size. 
 
    wBitmapX = GetSystemMetrics(SM_CXMENUCHECK); 
    wBitmapY = GetSystemMetrics(SM_CYMENUCHECK); 
 
    hbmpCheck = CreateCompatibleBitmap(hdcSource, wBitmapX, 
        wBitmapY); 
 
    // Select the background brush and bitmap into the target DC. 
 
    hbrTargetOld = SelectObject(hdcTarget, hbrBackground); 
    hbmpTargetOld = SelectObject(hdcTarget, hbmpCheck); 
 
    // Use the selected brush to initialize the background color 
    // of the bitmap in the target device context. 
 
    PatBlt(hdcTarget, 0, 0, wBitmapX, wBitmapY, PATCOPY); 
 
    // Load the predefined check box bitmaps and select it 
    // into the source DC. 
 
    hbmpCheckboxes = LoadBitmap((HINSTANCE) NULL, 
        (LPTSTR) OBM_CHECKBOXES); 
 
    hbmpSourceOld = SelectObject(hdcSource, hbmpCheckboxes); 
 
    // Fill a BITMAP structure with information about the 
    // check box bitmaps, and then find the upper-left corner of 
    // the unchecked check box or the checked check box. 
 
    GetObject(hbmpCheckboxes, sizeof(BITMAP), &bmCheckbox); 
 
    if (fuCheck == 2 /*UNCHECK*/) 
    { 
        rc.left = 0; 
        rc.right = (bmCheckbox.bmWidth / 4); 
    } 
    else 
    { 
        rc.left = (bmCheckbox.bmWidth / 4); 
        rc.right = (bmCheckbox.bmWidth / 4) * 2; 
    } 
 
    rc.top = 0; 
    rc.bottom = (bmCheckbox.bmHeight / 3); 
 
    // Copy the appropriate bitmap into the target DC. If the 
    // check-box bitmap is larger than the default check-mark 
    // bitmap, use StretchBlt to make it fit; otherwise, just 
    // copy it. 
 
    if (((rc.right - rc.left) > (int) wBitmapX) || 
            ((rc.bottom - rc.top) > (int) wBitmapY)) 
    {
        StretchBlt(hdcTarget, 0, 0, wBitmapX, wBitmapY, 
            hdcSource, rc.left, rc.top, rc.right - rc.left, 
            rc.bottom - rc.top, SRCCOPY); 
    }
 
    else 
    {
        BitBlt(hdcTarget, 0, 0, rc.right - rc.left, 
            rc.bottom - rc.top, 
            hdcSource, rc.left, rc.top, SRCCOPY); 
    }
 
    // Select the old source and destination bitmaps into the 
    // source and destination DCs, and then delete the DCs and 
    // the background brush. 
 
    SelectObject(hdcSource, hbmpSourceOld); 
    SelectObject(hdcTarget, hbrTargetOld); 
    hbmpCheck = SelectObject(hdcTarget, hbmpTargetOld); 
 
    DeleteObject(hbrBackground); 
    DeleteObject(hdcSource); 
    DeleteObject(hdcTarget); 
 
    // Return a handle to the new check-mark bitmap.  
 
    return hbmpCheck; 
} 

// Procedure de creation de menu à partir d'une clé de registre
HMENU InitLauncherMenu( char * Key ) {
	HMENU menu ;
	menu = CreatePopupMenu() ;
	char KeyName[1024] ;
	int nbitem = 0,i ;
	
	DeleteObject( bmpCheck ) ; bmpCheck = GetMyCheckBitmaps( 1 ) ;
	DeleteObject( bmpUnCheck ) ; bmpUnCheck = GetMyCheckBitmaps( 2 ) ;
	
	if( (IniFileFlag == SAVEMODE_REG)||(IniFileFlag == SAVEMODE_FILE) ) {
		sprintf( KeyName, "%s\\%s", TEXT(PUTTY_REG_POS), Key ) ;
		ReadSpecialMenu( menu, KeyName, &nbitem, 0 ) ;
	} else if( IniFileFlag == SAVEMODE_DIR ) {
		ReadSpecialMenu( menu, Key, &nbitem, 0 ) ;
	}

	if( GetMenuItemCount( menu ) > 0 )
		AppendMenu( menu, MF_SEPARATOR, 0, 0 ) ;

	// Creation du menu bouton gauche
	DestroyMenu( HideMenu ) ;
	HideMenu = CreatePopupMenu() ;
	if( !IsUnique ) {
		AppendMenu( HideMenu, MF_ENABLED, IDM_LAUNCHER+3, "&Hide all" ) ;
		AppendMenu( HideMenu, MF_ENABLED, IDM_LAUNCHER+4, "&Unhide all" ) ;
		//AppendMenu( HideMenu, MF_ENABLED, IDM_LAUNCHER+5, "&Refresh list" ) ;
		AppendMenu( HideMenu, MF_ENABLED, IDM_LAUNCHER+6, "&Window unique" ) ;
		CheckMenuItem( HideMenu, IDM_LAUNCHER+6, MF_BYCOMMAND | MF_UNCHECKED) ;
	} else {
		AppendMenu( HideMenu, MF_ENABLED, IDM_LAUNCHER+6, "&Window unique" ) ;
		CheckMenuItem( HideMenu, IDM_LAUNCHER+6, MF_BYCOMMAND | MF_CHECKED) ;
	}
	//AppendMenu( HideMenu, MF_ENABLED, IDM_GONEXT, "&Next" ) ;
	//AppendMenu( HideMenu, MF_ENABLED, IDM_GOPREVIOUS, "&Previous" ) ;
	if( RefreshWinList( MainHwnd ) > 0 ) {
		AppendMenu( HideMenu, MF_SEPARATOR, 0, 0 ) ;
		for( i=0 ; i<NbWin ; i++ ) {
			AppendMenu( HideMenu, MF_ENABLED, IDM_GOHIDE+i, TabWin[i].name ) ;
			SetMenuItemBitmaps ( HideMenu, IDM_GOHIDE+i, MF_BYCOMMAND, bmpUnCheck, bmpCheck ) ;
			if( IsWindowVisible( TabWin[i].hwnd ) ) 
				CheckMenuItem( HideMenu, IDM_GOHIDE+i, MF_BYCOMMAND | MF_CHECKED) ;
			else 
				CheckMenuItem( HideMenu, IDM_GOHIDE+i, MF_BYCOMMAND | MF_UNCHECKED) ;
		}
	}
	AppendMenu( HideMenu, MF_SEPARATOR, 0, 0 ) ;
	AppendMenu( HideMenu, MF_ENABLED, IDM_ABOUT, "&About" ) ;
	AppendMenu( HideMenu, MF_ENABLED, IDM_QUIT, "&Quit" ) ;

	
	AppendMenu( menu, MF_POPUP, (UINT_PTR)HideMenu, "&Opened sessions" ) ;
	AppendMenu( menu, MF_SEPARATOR, 0, 0 ) ;
	
	AppendMenu( menu, MF_ENABLED, IDM_LAUNCHER+7, "&Refresh" ) ;
	AppendMenu( menu, MF_ENABLED, IDM_LAUNCHER+1, "&Configuration" ) ;
	AppendMenu( menu, MF_ENABLED, IDM_LAUNCHER+2, "&TTY-ed" ) ;
	AppendMenu( menu, MF_SEPARATOR, 0, 0 ) ;
	AppendMenu( menu, MF_ENABLED, IDM_ABOUT, "&About" ) ;
	AppendMenu( menu, MF_ENABLED, IDM_QUIT, "&Quit" ) ;

	return menu ;
}

void RefreshMenuLauncher( void ) {
	DestroyMenu( MenuLauncher ) ; 
	MenuLauncher = NULL ;
	MenuLauncher = InitLauncherMenu( "Launcher" ) ;
}
	
// Nettoie les noms de folder en remplaçant les "/" par des "\" et les " \ " par des " \"
// Deplace dans kitty_commun.c
/*
void CleanFolderName( char * folder ) {
	int i, j ;
	if( folder == NULL ) return ;
	if( strlen( folder ) == 0 ) return ;
	for( i=0 ; i<strlen(folder) ; i++ ) if( folder[i]=='/' ) folder[i]='\\' ;
	for( i=0 ; i<(strlen(folder)-1) ; i++ ) 
		if( folder[i]=='\\' ) 
			while( folder[i+1]==' ' ) for( j=i+1 ; j<strlen(folder) ; j++ ) folder[j]=folder[j+1] ;
	for( i=(strlen(folder)-1) ; i>0 ; i-- )
		if( folder[i]=='\\' )
			while( folder[i-1]==' ' ) {
				for( j=i-1 ; j<strlen(folder) ; j++ ) folder[j]=folder[j+1] ;
				i-- ;
				}
	}
*/

// Supprime une arborescence   ==> deplace dans kitty_commun.c
/*
void DelDir( const char * directory ) {
	DIR * dir ;
	struct dirent * de ;
	char fullpath[MAX_VALUE_NAME] ;

	if( (dir=opendir(directory)) != NULL ) {
		while( (de=readdir( dir ) ) != NULL ) 
		if( strcmp(de->d_name,".") && strcmp(de->d_name,"..") ) {
			sprintf( fullpath, "%s\\%s", directory, de->d_name ) ;
			if( GetFileAttributes( fullpath ) & FILE_ATTRIBUTE_DIRECTORY ) { DelDir( fullpath ) ; }
			else if( !(GetFileAttributes( fullpath ) & FILE_ATTRIBUTE_DIRECTORY) ) { unlink( fullpath ) ; }
			}
		closedir( dir ) ;
		_rmdir( directory ) ;
		}
	}
*/

// Initialise l'arborescence Launcher en mode savemode=dir avec arborescence
void InitLauncherDir( const char * directory ) {
	char fullpath[MAX_VALUE_NAME], buffer[MAX_VALUE_NAME] ;
	DIR * dir ;
	struct dirent * de ;
	FILE * fp ;
	
	if( strlen(directory)>0 ) {
		sprintf( fullpath, "%s\\Sessions\\%s", ConfigDirectory, directory ) ;
		sprintf( buffer, "%s\\Launcher\\%s", ConfigDirectory, directory ) ;
	} else {
		sprintf( fullpath, "%s\\Sessions", ConfigDirectory ) ;
		sprintf( buffer, "%s\\Launcher", ConfigDirectory ) ;
	}
	if( !MakeDir( buffer ) ) { 
		//MessageBox(NULL,buffer,"Error",MB_OK|MB_ICONERROR); 
		MessageBox(NULL,"Unable to create the menu launcher directory","Error",MB_OK|MB_ICONERROR); 
	}
	if( (dir=opendir(fullpath)) != NULL ) {
		while( (de=readdir(dir)) != NULL ) 
		if( strcmp(de->d_name,".") && strcmp(de->d_name,"..") )	{
			sprintf( fullpath, "%s\\Sessions\\%s\\%s", ConfigDirectory, directory, de->d_name ) ;
			if( !(GetFileAttributes( fullpath ) & FILE_ATTRIBUTE_DIRECTORY) ) {
				sprintf( buffer, "%s\\Launcher\\%s\\%s", ConfigDirectory, directory, de->d_name ) ;
				if( (fp=fopen(buffer,"wb")) != NULL ) {
					unmungestr( de->d_name, buffer, MAX_VALUE_NAME) ;
					fprintf( fp, "%s\\%s\\", buffer, directory ) ;
					fclose( fp ) ; 
					}
				}
			else if( (GetFileAttributes( fullpath ) & FILE_ATTRIBUTE_DIRECTORY) ) {
				sprintf( buffer, "%s\\%s", directory, de->d_name ) ;
				if( buffer[0]=='\\' ) InitLauncherDir( buffer+1 ) ;
				else InitLauncherDir( buffer ) ;
				}

			}
		}
	closedir( dir ) ;
	}

// Inititalise la clé de registre Launcher avec les sessions enregistrées
void InitLauncherRegistry( void ) {
	HKEY hKey ;
	char buffer[MAX_VALUE_NAME] ;
	int i;
	
	if( (IniFileFlag == SAVEMODE_REG)||(IniFileFlag == SAVEMODE_FILE) ) {
		TCHAR folder[MAX_VALUE_NAME], achClass[MAX_PATH] = TEXT("");
		DWORD   cchClassName=MAX_PATH,cSubKeys=0,cbMaxSubKey,cchMaxClass;
		DWORD	cValues,cchMaxValue,cbMaxValueData,cbSecurityDescriptor;
		FILETIME ftLastWriteTime;
	
		sprintf( buffer, "%s\\Launcher", PUTTY_REG_POS ) ;
		RegDelTree (HKEY_CURRENT_USER, buffer ) ;
		RegTestOrCreate( HKEY_CURRENT_USER, buffer, NULL, NULL ) ;
		sprintf( buffer, "%s\\Sessions", PUTTY_REG_POS ) ;
		if( RegOpenKeyEx( HKEY_CURRENT_USER, buffer, 0, KEY_READ, &hKey) != ERROR_SUCCESS ) return ;

		RegQueryInfoKey(hKey,achClass,&cchClassName,NULL,&cSubKeys,&cbMaxSubKey,&cchMaxClass,&cValues,&cchMaxValue,&cbMaxValueData,&cbSecurityDescriptor,&ftLastWriteTime);

		if( cSubKeys>0 )
			for (i=0; i<cSubKeys; i++) {
				DWORD cchValue = MAX_VALUE_NAME; 
				char lpData[4096] ;
				if( RegEnumKeyEx(hKey, i, lpData, &cchValue, NULL, NULL, NULL, &ftLastWriteTime) == ERROR_SUCCESS ) {
					sprintf( buffer,"%s\\Sessions\\%s", TEXT(PUTTY_REG_POS), lpData ) ;
					if( !GetValueData(HKEY_CURRENT_USER, buffer, "Folder", folder ) ) 
						{ strcpy( folder, "Default" ) ; }
					CleanFolderName( folder ) ;
					if( !strcmp( folder, "Default" ) || (strlen(folder)<=0) ) 
						sprintf( buffer, "%s\\Launcher", TEXT(PUTTY_REG_POS) ) ;
					else 
						sprintf( buffer, "%s\\Launcher\\%s", TEXT(PUTTY_REG_POS), folder ) ;
					strcpy( folder, "" ) ;
					unmungestr( lpData, folder, MAX_VALUE_NAME ) ;
					if( strlen(folder) > 0 )
						RegTestOrCreate( HKEY_CURRENT_USER, buffer, folder, folder ) ;
				}
			}
		RegCloseKey( hKey ) ;
	} else if( (IniFileFlag == SAVEMODE_DIR)&&(DirectoryBrowseFlag==0) ) {
		char fullpath[MAX_VALUE_NAME], folder[MAX_VALUE_NAME] ;
		DIR * dir ;
		struct dirent * de ;
		FILE * fp ;
		sprintf( fullpath, "%s\\Launcher", ConfigDirectory ) ;
		DelDir( fullpath ) ;
		if(!MakeDir( fullpath ) ) { MessageBox(NULL,"Unable to create the menu launcher directory","Error",MB_OK|MB_ICONERROR); }
		sprintf( fullpath, "%s\\Sessions", ConfigDirectory ) ;
		if( (dir=opendir(fullpath)) != NULL ) {
			while( (de=readdir(dir)) != NULL ) 
			if( strcmp(de->d_name,".") && strcmp(de->d_name,"..") )	{
				sprintf( fullpath, "%s\\Sessions\\%s", ConfigDirectory, de->d_name ) ;
				if( !(GetFileAttributes( fullpath ) & FILE_ATTRIBUTE_DIRECTORY) ) {
					strcpy( folder, "" ) ;
					unmungestr( de->d_name, buffer, MAX_VALUE_NAME) ;
					GetSessionFolderName( buffer, folder ) ;
					CleanFolderName( folder ) ;
					sprintf( buffer, "%s\\Launcher\\%s", ConfigDirectory, folder ) ;
					if( strcmp(folder,"Default") ) {
						MakeDir( buffer ) ;
						sprintf( buffer, "%s\\Launcher\\%s\\%s", ConfigDirectory, folder, de->d_name ) ;
					} else sprintf( buffer, "%s\\Launcher\\%s", ConfigDirectory, de->d_name ) ;
					if( (fp=fopen(buffer,"wb")) != NULL ) {
						unmungestr( de->d_name, buffer, MAX_VALUE_NAME) ;
						fprintf( fp, "%s\\%s\\", buffer, buffer ) ;
						fclose( fp ) ; 
					}
				}
			}
			closedir(dir) ;
		}
	} else if( (IniFileFlag == SAVEMODE_DIR)&&DirectoryBrowseFlag ) {
		char fullpath[MAX_VALUE_NAME] ;
		sprintf( fullpath, "%s\\Launcher", ConfigDirectory ) ;
		DelDir( fullpath ) ;
		if( !MakeDir( fullpath ) ) { MessageBox(NULL,"Unable to create the menu launcher directory","Error",MB_OK|MB_ICONERROR); }
		InitLauncherDir( "" ) ;
	}
}

void DisplayContextMenu( HWND hwnd, HMENU menu ) {
	HMENU hMenuPopup = menu ;
	POINT pt;
	
	SetForegroundWindow( hwnd ) ;
	GetCursorPos (&pt);
	TrackPopupMenu (hMenuPopup, TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
}
	
// Gestion Hide/UnHide all
static int CurrentVisibleWin = -1 ; /* -1 = toutes visibles */

void ManageHideOne( HWND hwnd ) { PostMessage( hwnd, WM_COMMAND, IDM_HIDE, 0 ) ; }
void ManageUnHideOne( HWND hwnd ) { PostMessage( hwnd, WM_COMMAND, IDM_UNHIDE, 0 ) ; }

BOOL CALLBACK RefreshWinListProc( HWND hwnd, LPARAM lParam ) {
	char buffer[256] ;
	GetClassName( hwnd, buffer, 256 ) ;
	
	if( !strcmp( buffer, KiTTYClassName ) )
	if( hwnd != MainHwnd ) {
		TabWin[NbWin].hwnd=hwnd ;
		GetWindowText( hwnd, TabWin[NbWin].name, 127 ) ;
		NbWin++ ;
	}

	return TRUE ;
}

int RefreshWinList( HWND hwnd ) {
	NbWin=0 ;
	EnumWindows( RefreshWinListProc, 0 ) ;
	return NbWin ;
}
	
void GoNext( HWND hwnd ) {
	int i ;
	if( RefreshWinList( hwnd ) > 1 ) 
	for( i=0 ; i<NbWin ; i++ ) {
		if( hwnd == TabWin[i].hwnd ) {
			ManageHideOne( hwnd ) ;
			if( i == (NbWin-1) ) {
				ManageUnHideOne( TabWin[0].hwnd ) ;
				SetFocus( TabWin[0].hwnd ) ;
			} else {
				ManageUnHideOne( TabWin[i+1].hwnd ) ;
				SetFocus( TabWin[i+1].hwnd ) ;
			}
			break ;
		}
	}
}

void GoPrevious( HWND hwnd ) {
	int i ;
	if( RefreshWinList( hwnd ) > 1 ) 
	for( i=0 ; i<NbWin ; i++ ) {
		if( hwnd == TabWin[i].hwnd ) {
			ManageHideOne( hwnd ) ;
			if( i == 0 ) {
				ManageUnHideOne( TabWin[NbWin-1].hwnd ) ;
				SetFocus( TabWin[NbWin-1].hwnd ) ;
			} else {
				ManageUnHideOne( TabWin[i-1].hwnd ) ;
				SetFocus( TabWin[i-1].hwnd ) ;
			}
			break ;
		}
	}
}

void ManageHideAll( HWND hwnd ) {
	int i ;
	if( RefreshWinList( hwnd ) > 0 ) {
		for( i=0 ; i<NbWin ; i++ ) {
			ManageHideOne( TabWin[i].hwnd ) ;
		}
	}
	CurrentVisibleWin = 0 ;
}

void ManageUnHideAll( HWND hwnd ) {
	int i ;
	if( RefreshWinList( hwnd ) > 0 ) {
		for( i=0 ; i<NbWin ; i++ ) ManageUnHideOne( TabWin[i].hwnd ) ;
	}
	CurrentVisibleWin = -1 ;
}
	
void ManageGoNext( HWND hwnd ) {
	if( CurrentVisibleWin == -1 ) return ;
	ManageHideOne( TabWin[CurrentVisibleWin].hwnd ) ;
	CurrentVisibleWin++ ;
	if( CurrentVisibleWin>=NbWin ) CurrentVisibleWin=0 ;
	ManageUnHideOne( TabWin[CurrentVisibleWin].hwnd ) ;
}

void ManageGoPrevious( HWND hwnd ) {
	if( CurrentVisibleWin == -1 ) return ;
	ManageHideOne( TabWin[CurrentVisibleWin].hwnd ) ;
	CurrentVisibleWin-- ;
	if( CurrentVisibleWin<0 ) CurrentVisibleWin=NbWin-1 ;
	ManageUnHideOne( TabWin[CurrentVisibleWin].hwnd ) ;
}
	
void ManageGo( const int n ) {
	if( CurrentVisibleWin == -1 ) return ;
	if( (n<0)||(n>=100) ) return ;
	ManageHideOne( TabWin[CurrentVisibleWin].hwnd ) ;
	CurrentVisibleWin = n ;
	ManageUnHideOne( TabWin[CurrentVisibleWin].hwnd ) ;
}
	
void ManageSwitch( const int n ) { 
	SendMessage( TabWin[n].hwnd, WM_COMMAND, IDM_SWITCH_HIDE, 0 ) ; 
	SetForegroundWindow( TabWin[n].hwnd ) ;
	SetFocus( TabWin[n].hwnd ) ;
}
	
// Procedures principales du launcher
LRESULT CALLBACK Launcher_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	int ResShell ;
	static UINT s_uTaskbarRestart;
	
	switch( uMsg ) {
		case WM_CREATE:
		s_uTaskbarRestart = RegisterWindowMessage(TEXT("TaskbarCreated"));
		MenuLauncher = InitLauncherMenu( "Launcher" ) ;
        
	// Initialisation de la structure NOTIFYICONDATA
	TrayIcone.cbSize = sizeof(TrayIcone);	// On alloue la taille nécessaire pour la structure
	if( oldIconFlag ) {
		TrayIcone.uID = IDI_BLACKBALL ;	// On lui donne un ID
		TrayIcone.hIcon = LoadIcon((HINSTANCE) GetModuleHandle (NULL), MAKEINTRESOURCE(IDI_BLACKBALL));
	} else {
		TrayIcone.uID = IDI_PUTTY_LAUNCH ;	// On lui donne un ID
		TrayIcone.hIcon = LoadIcon((HINSTANCE) GetModuleHandle (NULL), MAKEINTRESOURCE(IDI_PUTTY_LAUNCH));
	}
	TrayIcone.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;	// On lui indique les champs valables
	// On lui dit qu'il devra "écouter" son environement (clique de souris, etc)
	TrayIcone.uCallbackMessage = KLWM_NOTIFYICON;
#ifdef FLJ
	TrayIcone.szTip[1024] = (TCHAR*)"PuTTY\0" ;			// Le tooltip par défaut, soit rien
#else
	//TrayIcone.szTip[1024] = "KiTTY That\'s all folks!\0" ;			// Le tooltip par défaut, soit rien
	strcpy( TrayIcone.szTip, "KiTTY That\'s all folks!\0" ) ;			// Le tooltip par défaut, soit rien
#endif
	TrayIcone.hWnd = hwnd ;
	ResShell = Shell_NotifyIcon(NIM_ADD, &TrayIcone);
	if( ResShell ) {
#ifdef FLJ
		strcpy( TrayIcone.szTip, "PuTTY\0" ) ;
#else
		strcpy( TrayIcone.szTip, "KiTTY That\'s all folks!\0" ) ;
#endif
		ResShell = Shell_NotifyIcon(NIM_MODIFY, &TrayIcone);
		if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
		//SendMessage(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		return 1 ;
	} else 
		return 0 ;
			break ;
	
		case KLWM_NOTIFYICON :
			switch (lParam)	{
				/*
				case WM_LBUTTONDBLCLK : 
					ShowWindow(hwnd, SW_SHOWNORMAL);
					SetForegroundWindow( hwnd ) ;
					int ResShell;
					ResShell = Shell_NotifyIcon(NIM_DELETE, &TrayIcone);
					if( ResShell ) return 1 ;
					else return 0 ;
				break ;
				*/
				case WM_RBUTTONUP:
					{
					if ( (wParam == IDI_PUTTY_LAUNCH) || (wParam == IDI_BLACKBALL) ) {
						RefreshMenuLauncher() ;
						DisplayContextMenu( hwnd, HideMenu ) ;
						}
					}
				break ;
				case WM_LBUTTONUP: 
					{
					if ( (wParam == IDI_PUTTY_LAUNCH) || (wParam == IDI_BLACKBALL) ) {
						RefreshMenuLauncher() ;
						DisplayContextMenu( hwnd, MenuLauncher ) ;
						}
					}
				break ;
				}
			break ;
	
		case WM_DESTROY: 
			ManageUnHideAll( hwnd ) ;
			PostQuitMessage( 0 ) ;
			break ;
		case WM_CLOSE:
			PostMessage(hwnd, WM_DESTROY,0,0) ;
			break ;
		case WM_COMMAND: {//Commandes du menu
			switch( LOWORD(wParam) ) {
				case IDM_ABOUT:
					MessageBox(hwnd,"     TTY Launcher\nSession launcher for TTY terminal emulator\n(c), 2009-2023","About", MB_OK ) ;
					break ;
				case IDM_QUIT:
					ResShell = Shell_NotifyIcon(NIM_DELETE, &TrayIcone) ;
					ManageUnHideAll( hwnd ) ;
					PostQuitMessage( 0 ) ;
					break ;
				case IDM_LAUNCHER:
					DestroyMenu( MenuLauncher ) ; 
					MenuLauncher = NULL ;
					MenuLauncher = InitLauncherMenu( "Launcher" ) ;
					Shell_NotifyIcon(NIM_DELETE, &TrayIcone);
					Shell_NotifyIcon(NIM_ADD, &TrayIcone);
					Shell_NotifyIcon(NIM_MODIFY, &TrayIcone);
					break ;
				case IDM_LAUNCHER+1:
					RunPuTTY( hwnd, "" ) ;
					break ;
				case IDM_LAUNCHER+2:
					RunPuTTY( hwnd, "-ed" ) ;
					break ;
				case IDM_LAUNCHER+3:
					ManageHideAll( hwnd ) ;
					RefreshMenuLauncher() ;
					break ;
				case IDM_LAUNCHER+4:
					ManageUnHideAll( hwnd ) ;
					RefreshMenuLauncher() ;
					break ;
				case IDM_LAUNCHER+5:
					RefreshMenuLauncher() ;
					break ;
				case IDM_LAUNCHER+6:
					IsUnique = abs( IsUnique -1 ) ;
					RefreshMenuLauncher() ;
					break ;
				case IDM_LAUNCHER+7:
					if( LauncherConfReload ) InitLauncherRegistry() ;
					RefreshMenuLauncher() ;
					break ;
				case IDM_GONEXT:
					ManageGoNext( hwnd ) ;
					break ;
				case IDM_GOPREVIOUS:
					ManageGoPrevious( hwnd ) ;
					break ;
				}
				int nb ;
				nb = LOWORD(wParam)-IDM_USERCMD ;
				if( ( nb >= 0 ) && ( nb<NB_MENU_MAX ) ) {
					if( SpecialMenu[nb]!= NULL )
					//if( strlen( SpecialMenu[nb] ) > 0 ) 
						{
						if( DirectoryBrowseFlag ) {
							char buffer[1024]="" ;
							GetMenuString( MenuLauncher, nb+IDM_USERCMD, buffer, 1024, MF_BYCOMMAND ) ;
							RunSession( hwnd, SpecialMenu[nb], buffer ) ;
							}
						else RunSession( hwnd, SpecialMenu[nb], SpecialMenu[nb] ) ;
						RefreshMenuLauncher() ;
						}
					break ;
					}
				nb = LOWORD(wParam)-IDM_GOHIDE ;
				if( ( nb >= 0 ) && ( nb<100 ) ) {
					if( !IsUnique )	ManageSwitch( nb ) ;
					else { 
						ManageHideAll( hwnd ) ; 
						ManageUnHideOne( TabWin[nb].hwnd ) ;
						}
					RefreshMenuLauncher() ;
					break ;
					}
				}
			break ;
		default: // Message par défaut
			if( uMsg == s_uTaskbarRestart ) { // On reaffiche l'icone après un crash de l'explorateur windows
				Shell_NotifyIcon(NIM_DELETE, &TrayIcone);
				Shell_NotifyIcon(NIM_ADD, &TrayIcone);
				Shell_NotifyIcon(NIM_MODIFY, &TrayIcone);
			}
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	return -1 ;
}
	
int WINAPI Launcher_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
	hinst = inst ;
	WNDCLASS wndclass ;
	MSG msg;
	char buffer[4096] ;
	char className[1024] = "KiTTYLauncher" ;
	
	if( strcmp(KiTTYClassName,appname) ) { strcpy(className,KiTTYClassName) ; }
	else if( strcmp(KiTTYClassName,"KiTTY") ) { strcpy(className,KiTTYClassName) ; }
	if( ReadParameter( "Launcher", "classname", buffer ) ) {
		buffer[1023]='\0' ;
		if( strlen(buffer)>0 ) { strcpy(className,buffer) ; }
	}
	
	if( FindWindow(className,className) ) {
		if( ReadParameter( "Launcher", "alreadyRunCheck", buffer ) ) {
			if( !stricmp( buffer, "yes" ) ) return 0 ;
		} else { 
			return 0 ; 
		}
	}

	if( strstr( cmdline, "-putty" ) != NULL ) SetPuttyFlag(1) ;

	wndclass.style = 0;
	wndclass.lpfnWndProc = Launcher_WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = inst;
	if( strstr( cmdline, "-oldicon" ) != NULL ) { oldIconFlag = 1 ; } 
	if( oldIconFlag ) { wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_BLACKBALL) ); }
	else { wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_PUTTY_LAUNCH) ); }
	wndclass.hCursor = LoadCursor(NULL, IDC_IBEAM) ;
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = className ;

	if( !RegisterClass(&wndclass) ) return 1 ;

	if( ReadParameter( "Launcher", "reload", buffer ) ) {
		if( !stricmp( buffer, "NO" ) ) LauncherConfReload = 0 ;
	}
	if( LauncherConfReload ) InitLauncherRegistry() ;
		
	MainHwnd = CreateWindowEx(0, className, "KiTTYLauncher",
				0,//WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT, CW_USEDEFAULT,
				CW_USEDEFAULT, CW_USEDEFAULT,
				NULL, NULL, inst, NULL);
	
	//ShowWindow(hwnd, show) ; UpdateWindow(hwnd) ;

	while (GetMessage(&msg, NULL, 0, 0)) {
		//if(!TranslateAccelerator(hwnd, hAccel, &msg)){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		//	}
	}
	return msg.wParam;
}

#endif
void RunConfig( Conf * conf ) {
	char b[2048];
	//char c[180];
	//int freecl = FALSE;
	char *cl;
	const char *argprefix;
	BOOL inherit_handles;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	HANDLE filemap = NULL;
	
	char bufpass[1024] ;
	strcpy( bufpass, conf_get_str(conf,CONF_password)) ;
	MASKPASS(GetCryptSaltFlag(),bufpass) ;
	conf_set_str(conf,CONF_password,bufpass) ;
	
	if (restricted_acl) {
		argprefix = "&R";
	} else {
		argprefix = "";
	}
	/*
	 * Allocate a file-mapping memory chunk for the
	 * config structure.
	 */
	SECURITY_ATTRIBUTES sa;
	strbuf *serbuf;
	void *p;
	int size;

	serbuf = strbuf_new();
	conf_serialise(BinarySink_UPCAST(serbuf), conf);
	size = serbuf->len;

	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	filemap = CreateFileMapping(INVALID_HANDLE_VALUE,
		&sa,
		PAGE_READWRITE,
		0, size, NULL);
	if (filemap && filemap != INVALID_HANDLE_VALUE) {
		p = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, size);
		if (p) {
			memcpy(p, serbuf->s, size);
			UnmapViewOfFile(p);
		}
	}
	strbuf_free(serbuf);
	inherit_handles = true;
	cl = dupprintf("putty %s&%p:%u", argprefix,
		filemap, (unsigned)size);
		    
	MASKPASS(GetCryptSaltFlag(),bufpass);
	conf_set_str(conf,CONF_password,bufpass);
	memset(bufpass,0,strlen(bufpass));
		    
	GetModuleFileName(NULL, b, sizeof(b) - 1);
	si.cb = sizeof(si);
	si.lpReserved = NULL;
	si.lpDesktop = NULL;
	si.lpTitle = NULL;
	si.dwFlags = 0;
	si.cbReserved2 = 0;
	si.lpReserved2 = NULL;
	CreateProcess(b, cl, NULL, NULL, inherit_handles,
		NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
	CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

	if (filemap)
		CloseHandle(filemap);
	sfree(cl);
}

void RunPuTTY( HWND hwnd, char * param ) {
	char buffer[4096]="",shortname[1024]="" ; ;
	if( GetModuleFileName( NULL, (LPTSTR)buffer, 1023 ) ) 
		if( GetShortPathName( buffer, shortname, 1023 ) ) {
			if( strlen(param) > 0 ) 
				sprintf( buffer, "%s %s", shortname, param ) ;
			else 
				strcpy( buffer, shortname ) ;
			RunCommand( hwnd, buffer ) ;
		}
}

int RunSession( HWND hwnd, const char * folder_in, char * session_in ) {
	char buffer[4096]="", shortname[1024]="" ;
	char *session=NULL ;
	int return_code=0 ;
	
	if( session_in==NULL ) return 0 ;
	if( strlen(session_in) <= 0 ) return 0 ;
		
	if( !GetModuleFileName( NULL, (LPTSTR)buffer, 1023 ) ) return 0 ;
	if( !GetShortPathName( buffer, shortname, 1023 ) ) return 0 ;

	session = (char*)malloc(strlen(session_in)+100) ;
	
	if( (IniFileFlag==SAVEMODE_REG)||(IniFileFlag==SAVEMODE_FILE) ) {
		mungestr(session_in, session) ;
		sprintf( buffer, "%s\\Sessions\\%s", TEXT(PUTTY_REG_POS), session ) ;
		if( RegTestKey(HKEY_CURRENT_USER, buffer) ) {
			strcpy( session, session_in ) ;
			if( session[strlen(session)-1] == '&' ) {
				session[strlen(session)-1]='\0' ;
				while( (session[strlen(session)-1]==' ')||(session[strlen(session)-1]=='\t') ) session[strlen(session)-1]='\0' ;
				if( GetPuttyFlag() )	sprintf( buffer, "%s -putty -load \"%s\" -send-to-tray", shortname, session ) ;
				else sprintf( buffer, "%s -load \"%s\" -send-to-tray", shortname, session ) ;
			} else {
				if( GetPuttyFlag() )	sprintf( buffer, "%s -putty -load \"%s\"", shortname, session ) ;
				else sprintf( buffer, "%s -load \"%s\"", shortname, session ) ;
			}
			RunCommand( hwnd, buffer ) ;
			return_code = 1 ;
		} else { 
			RunCommand( hwnd, session_in ) ; 
		}
	} else if( IniFileFlag==SAVEMODE_DIR ) {
		if( DirectoryBrowseFlag ) {
			if( (folder_in!=NULL)&&strcmp(folder_in,"")&&strcmp(folder_in,"Default") ) {
				strcat( shortname, " -folder \"" ) ;
				strcat( shortname, folder_in ) ;
				strcat( shortname, "\"" ) ;
			}
		}
		strcpy( session, session_in ) ;
		if( session[strlen(session)-1] == '&' ) {
			session[strlen(session)-1]='\0' ;
			while( (session[strlen(session)-1]==' ')||(session[strlen(session)-1]=='\t') ) session[strlen(session)-1]='\0' ;
			if( GetPuttyFlag() )	sprintf( buffer, "%s -putty -load \"%s\" -send-to-tray", shortname, session ) ;
			else sprintf( buffer, "%s -load \"%s\" -send-to-tray", shortname, session ) ;
		} else {
			if( GetPuttyFlag() )	sprintf( buffer, "%s -putty -load \"%s\"", shortname, session ) ;
			else sprintf( buffer, "%s -load \"%s\"", shortname, session ) ;
			//else sprintf( buffer, "%s @%s", shortname, session ) ;
		}
/*		if( DirectoryBrowseFlag ) {
			if( strcmp(folder_in,"")&&strcmp(folder_in,"Default") ) {
				strcat( buffer, " -folder \"" ) ;
				strcat( buffer, folder_in ) ;
				strcat( buffer, "\"" ) ;
				}
			}*/
//MessageBox( hwnd, buffer, "Info", MB_OK ) ;
		RunCommand( hwnd, buffer ) ;
		return_code = 1 ;
	}

	free( session ) ;
	return return_code ;
}
