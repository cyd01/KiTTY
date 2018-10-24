#include "kitty_win.h"

void logevent(void *frontend, const char *);

// Modifie la transparence
void SetTransparency( HWND hwnd, int value ) {
#ifndef NO_TRANSPARENCY
	SetLayeredWindowAttributes( hwnd, 0, value, LWA_ALPHA ) ;
#endif
	}


// Numéro de version de l'OS
void GetOSInfo( char * version ) {
	OSVERSIONINFO osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osvi);
	sprintf( version, "%ld.%ld %ld %ld %s %dx%d", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber, osvi.dwPlatformId, osvi.szCSDVersion, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) ) ;
	}
/*
http://msdn.microsoft.com/en-us/library/windows/desktop/ms724832%28v=vs.85%29.aspx
Operating system 			Version number
Windows 10 Insider Preview		10.0*
Windows Server Technical Preview	10.0*
Windows 8.1				6.3*
Windows Server 2012 R2			6.3*
Windows 8				6.2
Windows Server 2012			6.2
Windows 7				6.1
Windows Server 2008 R2			6.1
Windows Server 2008			6.0
Windows Vista				6.0
Windows Server 2003 R2			5.2
Windows Server 2003			5.2
Windows XP 64-Bit Edition		5.2
Windows XP				5.1
Windows 2000				5.0
*/

/*
https://msdn.microsoft.com/en-us/library/aa383745%28v=vs.85%29.aspx#faster_builds_with_smaller_header_files
Minimum system required					Minimum value for _WIN32_WINNT and WINVER
Windows 8.1						_WIN32_WINNT_WINBLUE (0x0602)
Windows 8						_WIN32_WINNT_WIN8 (0x0602)
Windows 7						_WIN32_WINNT_WIN7 (0x0601)
Windows Server 2008					_WIN32_WINNT_WS08 (0x0600)
Windows Vista						_WIN32_WINNT_VISTA (0x0600)
Windows Server 2003 with SP1, Windows XP with SP2	_WIN32_WINNT_WS03 (0x0502)
Windows Server 2003, Windows XP				_WIN32_WINNT_WINXP (0x0501)

Minimum version required		Minimum value of _WIN32_IE
Internet Explorer 10.0			_WIN32_IE_IE100 (0x0A00)
Internet Explorer 9.0			_WIN32_IE_IE90 (0x0900)
Internet Explorer 8.0			_WIN32_IE_IE80 (0x0800)
Internet Explorer 7.0			_WIN32_IE_IE70 (0x0700)
Internet Explorer 6.0 SP2		_WIN32_IE_IE60SP2 (0x0603)
Internet Explorer 6.0 SP1		_WIN32_IE_IE60SP1 (0x0601)
Internet Explorer 6.0			_WIN32_IE_IE60 (0x0600)
Internet Explorer 5.5			_WIN32_IE_IE55 (0x0550)
Internet Explorer 5.01			_WIN32_IE_IE501 (0x0501)
Internet Explorer 5.0, 5.0a, 5.0b	_WIN32_IE_IE50 (0x0500)
*/

typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
LPFN_ISWOW64PROCESS fnIsWow64Process;
BOOL IsWow64() {
    BOOL bIsWow64 = FALSE;
    //IsWow64Process is not available on all supported versions of Windows.
    //Use GetModuleHandle to get a handle to the DLL that contains the function
    //and GetProcAddress to get a pointer to the function if available.
    fnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(
        GetModuleHandle(TEXT("kernel32")),"IsWow64Process");
    if(NULL != fnIsWow64Process) {
        if (!fnIsWow64Process(GetCurrentProcess(),&bIsWow64)) {
            //handle error
        }
    }
    return bIsWow64 ;
}

int OpenFileName( HWND hFrame, char * filename, char * Title, char * Filter ) {
	char * szTitle = Title ;
	char szFilter[256] ; strcpy( szFilter, Filter ) ;
	// on remplace les caractères '|' par des caractères NULL.
	int i = 0;
	while(i < sizeof(szFilter) && szFilter[i] != '\0')
	{
		if(szFilter[i] == '|')
			szFilter[i] = '\0';

		i++;
	}

	// boîte de dialogue de demande d'ouverture de fichier
	//char szFileName[_MAX_PATH + 1] = "";
	char * szFileName = filename ;
	szFileName[0] = '\0' ;
	OPENFILENAME ofn	= {0};
	ofn.lStructSize		= sizeof(OPENFILENAME);
	ofn.hwndOwner		= hFrame;
	ofn.lpstrFilter		= szFilter;
	ofn.nFilterIndex	= 1;
	ofn.lpstrFile		= szFileName;
	//ofn.nMaxFile		= sizeof(szFileName);
	ofn.nMaxFile		= 4096 ;
	ofn.lpstrTitle		= szTitle;
	ofn.Flags		= OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST 
				| OFN_HIDEREADONLY | OFN_LONGNAMES
				| OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_EXTENSIONDIFFERENT | OFN_DONTADDTORECENT
				;

	// si aucun nom de fichier n'a été sélectionné, on abandonne
	if(!GetOpenFileName(&ofn)) { return 0 ; }
	else { return 1 ; }
	}

int SaveFileName( HWND hFrame, char * filename, char * Title, char * Filter ) {
	char * szTitle = Title ;
	char szFilter[256] ; strcpy( szFilter, Filter ) ;
	// on remplace les caractères '|' par des caractères NULL.
	int i = 0;
	while(i < sizeof(szFilter) && szFilter[i] != '\0')
	{
		if(szFilter[i] == '|')
			szFilter[i] = '\0';

		i++;
	}

	// boîte de dialogue de demande d'ouverture de fichier
	//char szFileName[_MAX_PATH + 1] = "";
	char * szFileName = filename ;
	szFileName[0] = '\0' ;
	OPENFILENAME ofn	= {0};
	ofn.lStructSize		= sizeof(OPENFILENAME);
	ofn.hwndOwner		= hFrame;
	ofn.lpstrFilter		= szFilter;
	ofn.nFilterIndex	= 1;
	ofn.lpstrFile		= szFileName;
	//ofn.nMaxFile		= sizeof(szFileName);
	ofn.nMaxFile		= 4096 ;
	ofn.lpstrTitle		= szTitle;
	ofn.lpstrDefExt 	= ".ktx" ;
	ofn.Flags		= OFN_PATHMUSTEXIST 
				| OFN_HIDEREADONLY | OFN_LONGNAMES | OFN_OVERWRITEPROMPT
				| OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_EXTENSIONDIFFERENT | OFN_DONTADDTORECENT
				;

	// si aucun nom de fichier n'a été sélectionné, on abandonne
	if(!GetSaveFileName(&ofn)) { return 0 ; }
	else { return 1 ; }
	}

#include <shlobj.h>
int OpenDirName( HWND hFrame, char * dirname ) {
	BROWSEINFO bi ;
	ITEMIDLIST *il ;
	LPITEMIDLIST ol = NULL ;
	char Buffer[4096],Result[4096]="" ;
	dirname[0]='\0' ;

	strcpy( Buffer, getenv("ProgramFiles") ) ;
	
	//SHGetSpecialFolderLocation( hFrame, CSIDL_MYDOCUMENTS, &ol );
	
	memset(&bi,0,sizeof(BROWSEINFO));
	bi.hwndOwner = hFrame ;
	//bi.pidlRoot=NULL ; //
	bi.pidlRoot=ol ;
	bi.pszDisplayName=&Buffer[0];
	bi.lpszTitle="Select a folder...";
	bi.ulFlags=0;
	bi.lpfn=NULL;
	if ((il=SHBrowseForFolder(&bi))!=NULL) {
		SHGetPathFromIDList(il,&Result[0]) ;
		//ILFree( il ) ; ILFree( ol ) ;
		GlobalFree(il);GlobalFree(ol);
		if( strlen( Result ) == 0 ) return 0 ;
		strcpy( dirname, Result ) ;
		return 1 ;
		}
	//ILFree( ol ) ;
	GlobalFree(ol);
	return 0 ;
	}

// Centre un dialog au milieu de la fenetre parent
void CenterDlgInParent(HWND hDlg) {
  RECT rcDlg;
  HWND hParent;
  RECT rcParent;
  MONITORINFO mi;
  HMONITOR hMonitor;

  int xMin, yMin, xMax, yMax, x, y;

  GetWindowRect(hDlg,&rcDlg);

  hParent = GetParent(hDlg);
  GetWindowRect(hParent,&rcParent);

  hMonitor = MonitorFromRect(&rcParent,MONITOR_DEFAULTTONEAREST);
  mi.cbSize = sizeof(mi);
  GetMonitorInfo(hMonitor,&mi);

  xMin = mi.rcWork.left;
  yMin = mi.rcWork.top;

  xMax = (mi.rcWork.right) - (rcDlg.right - rcDlg.left);
  yMax = (mi.rcWork.bottom) - (rcDlg.bottom - rcDlg.top);

  if ((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left) > 20)
    x = rcParent.left + (((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left)) / 2);
  else
    x = rcParent.left + 70;

  if ((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top) > 20)
    y = rcParent.top  + (((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top)) / 2);
  else
    y = rcParent.top + 60;

  SetWindowPos(hDlg,NULL,max(xMin,min(xMax,x)),max(yMin,min(yMax,y)),0,0,SWP_NOZORDER|SWP_NOSIZE);
}


//
// Envoi vers l'imprimante
//
// Parametres de l'impression
int PrintCharSize = 100 ;
int PrintMaxLinePerPage = 60 ;
int PrintMaxCharPerLine = 85 ;

int PrintText( const char * Text ) {
	int return_code = 0 ; 
	PRINTDLG	pd;
	DOCINFO		di;
	int i, TextLen = 0, Index1 = 0, Index2 = 2, Exit = 0 ;
	char*		LinePrint = NULL ;
	char*		szMessage = NULL ;

	if( Text == NULL ) return 1 ;
	if( strlen( Text ) == 0 ) return 1 ;

	memset (&pd, 0, sizeof(PRINTDLG));
	memset (&di, 0, sizeof(DOCINFO));

	di.cbSize = sizeof(DOCINFO);
	di.lpszDocName = "Test";

	pd.lStructSize = sizeof(PRINTDLG);
	pd.Flags = PD_PAGENUMS | PD_RETURNDC;
	pd.nFromPage = 1;
	pd.nToPage = 1;
	pd.nMinPage = 1;
	pd.nMaxPage = 1;
	szMessage = 0;

	if( PrintDlg( &pd ) ) {
		if( pd.hDC ) {
			if (StartDoc (pd.hDC, &di) != SP_ERROR)	{
				TextLen = strlen( Text ) ;
				if( TextLen > 0 ) {
					LinePrint = (char*) malloc( TextLen + 2 ) ;
					Index1 = 0 ; Index2 = 2 ; Exit = 0 ;
					for( i = 0 ; i < TextLen ; i++ ) {
						if( Text[i]=='\r' ) i++;
                    				LinePrint[Index1] = Text[i] ;
                    				if( Text[i] == '\n' ) {
                      					Index2++ ;
							LinePrint[Index1] = '\0' ;
                      					TextOut(pd.hDC,100, Index2*PrintCharSize, LinePrint, strlen(LinePrint) ) ;
							Index1 = 0 ;
                    					}
						else if( Index1>=PrintMaxCharPerLine ) {
							Index2++ ;
							LinePrint[Index1+1] = '\0' ;
                      					TextOut(pd.hDC,100, Index2*PrintCharSize, LinePrint, strlen(LinePrint) ) ;
							Index1 = 0 ;
							}
                    				else { Index1++ ; }
                    				if( Index2 >= PrintMaxLinePerPage ) {
                  	   				EndPage( pd.hDC ) ;
                       					//EndDoc(pd.hDC) ;
                       					//StartDoc(pd.hDC, &di) ;
							StartPage( pd.hDC ) ;
                       					Index2 = 2 ;
                       					}
                  				}
                  			Index2++ ; 
                  			LinePrint[Index1] = '\0'; // Impression de la dernière page
                  			TextOut(pd.hDC,100, Index2*PrintCharSize, LinePrint, strlen(LinePrint)) ;
               	  			EndPage(pd.hDC) ;
                  			EndDoc(pd.hDC) ;
                  			szMessage = "Print successful";
					free( LinePrint ) ;
              				}
              			else { return_code = 1 ;  /* Chaine vide */ }
				}
			else { // Problème StartDoc
				szMessage = "ERROR Type 1" ;
				return_code = 2 ;
				}
			}
		else { // Probleme pd.hDC
			szMessage = "ERROR Type 2." ;
			return_code = 3 ;
			}
		}
	else { // Problème PrintDlg
		//szMessage = "Impression annulée par l'utilisateur" ;
		return_code = 4 ;
		}
	if (szMessage) { MessageBox (NULL, szMessage, "Print report", MB_OK) ; }
	
	return return_code ;
	}

// Impression du texte dans le bloc-notes
void ManagePrint( HWND hwnd ) {
	char *pst = NULL ;
	if( OpenClipboard(NULL) ) {
		HGLOBAL hglb ;
		
		if( (hglb = GetClipboardData( CF_TEXT ) ) != NULL ) {
			if( ( pst = GlobalLock( hglb ) ) != NULL ) {
				PrintText( pst ) ;
				GlobalUnlock( hglb ) ;
				}
			}

		CloseClipboard();
		}
	}

// Met un texte dans le press-papier
int SetTextToClipboard( const char * buf ) {
	HGLOBAL hglbCopy ;
	LPTSTR lptstrCopy ;
	
	if( !IsClipboardFormatAvailable(CF_TEXT) ) return 0 ;
	if( !OpenClipboard(NULL) ) return 0 ;
	
	EmptyClipboard() ; 
	if( (hglbCopy= GlobalAlloc(GMEM_MOVEABLE, (strlen(buf)+1) * sizeof(TCHAR)) ) == NULL )
		{ CloseClipboard() ; return 0 ;	}

	lptstrCopy = GlobalLock( hglbCopy ) ; 
	memcpy( lptstrCopy, buf, (strlen(buf)+1) * sizeof(TCHAR) ) ;
	GlobalUnlock( hglbCopy ) ; 
		
	if( SetClipboardData(CF_TEXT, hglbCopy) == NULL ) 
		{ CloseClipboard() ; return 0 ; }

	CloseClipboard() ;
	return 1 ;
	}

// Execute une commande	
void RunCommand( HWND hwnd, char * cmd ) {
	PROCESS_INFORMATION ProcessInformation ;
	ZeroMemory( &ProcessInformation, sizeof(ProcessInformation) );
	
	STARTUPINFO StartUpInfo ;
	ZeroMemory( &StartUpInfo, sizeof(StartUpInfo) );
	StartUpInfo.cb=sizeof(STARTUPINFO);
	StartUpInfo.lpReserved=0;
	StartUpInfo.lpDesktop=0;
	StartUpInfo.lpTitle=0;
	StartUpInfo.dwX=0;
	StartUpInfo.dwY=0;
	StartUpInfo.dwXSize=0;
	StartUpInfo.dwYSize=0;
	StartUpInfo.dwXCountChars=0;
	StartUpInfo.dwYCountChars=0;
	StartUpInfo.dwFillAttribute=0;
	StartUpInfo.dwFlags=0;
	StartUpInfo.wShowWindow=0;
	StartUpInfo.cbReserved2=0;
	StartUpInfo.lpReserved2=0;
	StartUpInfo.hStdInput=0;
	StartUpInfo.hStdOutput=0;
	StartUpInfo.hStdError=0;
//MessageBox(hwnd,cmd,"Info",MB_OK);

	if( !CreateProcess(NULL,(CHAR*)cmd,NULL,NULL,FALSE,NORMAL_PRIORITY_CLASS,NULL,NULL,&StartUpInfo,&ProcessInformation) ) {
		ShellExecute(hwnd, "open", cmd ,0 , 0, SW_SHOWDEFAULT);
		}
	else { 
		WaitForInputIdle(ProcessInformation.hProcess, INFINITE ); 
		CloseHandle( &StartUpInfo );
		CloseHandle( &ProcessInformation );
		}
	}

void RunPuttyEd( HWND hwnd, char * filename ) {
	char buffer[1024]="", shortname[1024]="" ;
	if( GetModuleFileName( NULL, (LPTSTR)buffer, 1023 ) ) 
		if( GetShortPathName( buffer, shortname, 1023 ) ) {
			strcat( shortname, " -ed" );
			if( filename!=NULL ) if( strlen(filename)>0 ) { strcat( shortname, "b " ) ; strcat( shortname, filename ) ; }
			logevent(NULL, shortname ) ;
			RunCommand( hwnd, shortname ) ; 
			}
	}

// Verifie si une mise a jour est disponible sur le site web
extern char BuildVersionTime[256] ;
void CheckVersionFromWebSite( HWND hwnd ) {
	char buffer[1024]="", vers[1024]="" ;
	int i ;
	strcpy( vers, BuildVersionTime ) ;
	for( i = 0 ; i < strlen( vers ) ; i ++ ) {
		if( !(((vers[i]>='0')&&(vers[i]<='9'))||(vers[i]=='.')) ) { vers[i] = '\0' ; break ; }
		}
	sprintf( buffer, "http://www.9bis.net/kitty/check_update.php?version=%s", vers ) ;
	ShellExecute(hwnd, "open", buffer, 0, 0, SW_SHOWDEFAULT);
	}
