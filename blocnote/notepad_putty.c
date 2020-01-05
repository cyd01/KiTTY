#include <time.h>
#include <math.h>

HWND ParentWindow = NULL ;		// HWND de la fenêtre parente

char * get_param_str( char * str ) ;

void TestIfParentIsKiTTY( void ) {
	ParentWindow = GetForegroundWindow() ;
	char buffer[256] ;
	GetClassName( ParentWindow, buffer, 256 ) ;
	if( strcmp( buffer, "KiTTY" ) && strcmp( buffer, "PuTTY" ) ) ParentWindow = NULL ;
		//SendMessage(ParentWindow, WM_CHAR, buffer[i], 0) ; 
	}

void ActiveParent( void ) { if( IsIconic( ParentWindow ) ) ShowWindow(ParentWindow,SW_RESTORE) ; }
	
void Send2Window( HWND SrcWnd, HWND TargetWnd, char * buffer ) {
	int i ;
	if( (buffer!=NULL) && (strlen( buffer) > 0 ) ) {
		if( IsIconic( TargetWnd ) ) { 
			ShowWindow( TargetWnd, SW_RESTORE ) ;
			for( i=0; i< strlen( buffer ) ; i++ ) 
				if( buffer[i] == '\n' ) { SendMessage( TargetWnd, WM_KEYDOWN, VK_RETURN, 0 ) ; }
				else if( buffer[i] == '\r' ) { }
				else SendMessage( TargetWnd, WM_CHAR, buffer[i], 0 ) ;
			if( SrcWnd!=NULL ) { SetForegroundWindow( SrcWnd ) ; }
		} else {
			BringWindowToTop( TargetWnd ) ;
			for( i=0; i< strlen( buffer ) ; i++ ) 
				if( buffer[i] == '\n' ) { SendMessage( TargetWnd, WM_KEYDOWN, VK_RETURN, 0 ) ; }
				else if( buffer[i] == '\r' ) { }
				else SendMessage( TargetWnd, WM_CHAR, buffer[i], 0 ) ;
			if ( SrcWnd!=NULL ) { BringWindowToTop( SrcWnd ) ; }
		}
	}
}
	
void Send2Parent( HWND ChildWnd , char * buffer ) {
	if( (ParentWindow != NULL) && (buffer!=NULL) ) {
		Send2Window( ChildWnd, ParentWindow, buffer ) ;
	}
}

void InitKiTTYNotepad( HWND hwnd ) {
	if( ParentWindow!=NULL ) {
		int pw=0, ph=0, px=0, py=0 ;
		RECT Rect ;
		GetWindowRect( ParentWindow, &Rect ) ;
		px=Rect.left+20 ; 
		py=Rect.top+20 ;
		pw=Rect.right-Rect.left+1 ;
		ph=Rect.bottom-Rect.top+1 ;
		SetWindowPos(hwnd, NULL, px, py, pw, ph, SWP_NOZORDER) ;
		}
	}

static int CRLF_flag = 1 ;
static int Semic_flag = 1 ;
static int Slash_flag = 0 ;
static int Tilde_flag = 0 ;
	
// Fonction pour récuperer le contenu de la fenêtre Edit
int GetEditContent( HWND hWndEdit, char * buffer ) {
	int d,f,i;
	char CharLim = '\n' ;
	
	if( Semic_flag ) CharLim = ';' ;
	if( Slash_flag ) CharLim = '/' ;
	if( Tilde_flag ) CharLim = '~' ;
	if( CRLF_flag ) CharLim = '\n' ;
	
	GetWindowText(hWndEdit,buffer,32000);
	SendMessage( hWndEdit, EM_GETSEL, (WPARAM)&d, (LPARAM)&f ) ;
	
	if( d==f ) {
		while( (d>0)&&(buffer[d-1]!=CharLim) ) d-- ;
		while( (buffer[f]!=CharLim)&&(buffer[f]!='\0') ) f++ ;
			
		if( buffer[f-1] != CharLim ) { buffer[f] = CharLim ; buffer[f+1] = '\0' ; f++ ; }
		else { buffer[f] = '\0' ; }
		
		SendMessage( hWndEdit, EM_SETSEL, (WPARAM)d, (LPARAM)f ) ;
		}
	else buffer[f] = '\0' ;

	if( Slash_flag ) if( buffer[f-1]=='/' ) {
		i = f-2 ;
		while( (i>0)&&( (buffer[i]==' ')||(buffer[i]=='	')||(buffer[i]=='\n')||(buffer[i]=='\r') ) ) i-- ;
		if( (i>0) && (buffer[i]==';') ) buffer[f-1]=' ' ;
		}

	if( CRLF_flag && (buffer[strlen(buffer)-1]!='\n') ) strcat( buffer, "\n" ) ;
	return d ;
}

void ChangeToCRLF( HWND hWndEdit ) {
	char buffer[32000];
	int i,j;
	if( GetWindowText(hWndEdit,buffer,32000)>0 ) if( buffer[0]!='\0' ) {
		i=0;
		while( buffer[i]!='\0' ) {
			if( (buffer[i]=='\r') && (buffer[i+1]!='\n') ) { 
				for( j=strlen(buffer)+1 ; j>(i+1) ; j-- ) { buffer[j] = buffer[j-1] ; }
				buffer[i+1] = '\n' ;
				i++ ;
			}
			i++;
		}
		i=0;
		while( buffer[i]!='\0' ) {
			if( (buffer[i]=='\n') && ((i==0)||(buffer[i-1]!='\r')) ) {
				for( j=strlen(buffer)+1 ; j>i ; j-- ) { buffer[j] = buffer[j-1] ; }
				buffer[i] = '\r' ;
				i++ ;
			}
			i++;
		}
		SetWindowText(hWndEdit,buffer);
	}
}
	
// Fonction pour envoyer une chaîne à la fenetre parente
void SendStrToParent( HWND hWndEdit ) {
	char buffer[32000] = "" ;
	int d = GetEditContent( hWndEdit, buffer ) ;
		
//sprintf( buffer,"#%d -> %d#",d,f);MessageBox(hWndEdit, buffer,"Info",MB_OK);GetWindowText(hWndEdit,buffer,32000);
	if( strlen(buffer)>0 ) {
		Send2Parent( GetParent( hWndEdit ), buffer+d ) ;
		SetFocus( hWndEdit ) ;
		SendMessage( hWndEdit, EM_SETSEL, (WPARAM)d, (LPARAM)d ) ;
		}
	}

// Fonctions pour envoyer une chaîne à toutes les fenêtres KiTTY
BOOL CALLBACK SendToAllProc( HWND hwnd, LPARAM lParam ) {
	char buffer[256] ;
	GetClassName( hwnd, buffer, 256 ) ;
	if( !strcmp( buffer, get_param_str("CLASS") ) )
		Send2Window( NULL, hwnd, (char*)lParam ) ;
	return TRUE ;
	}

void SendStrToAll( HWND hWndEdit ) {
	char buffer[32000] = "" ;
	int d = GetEditContent( hWndEdit, buffer ) ;
		
	if( strlen(buffer)>0 ) {
		EnumWindows( SendToAllProc, (LPARAM)(buffer+d) ) ;
		SetFocus( hWndEdit ) ;
		SendMessage( hWndEdit, EM_SETSEL, (WPARAM)d, (LPARAM)d ) ;
	}
}

void SetWindowsSize( HWND hwnd ) {
	//int w=GetSystemMetrics(SM_CXSCREEN), h=GetSystemMetrics(SM_CYSCREEN) ;
	int w=GetSystemMetrics(SM_CXFULLSCREEN), h=GetSystemMetrics(SM_CYFULLSCREEN) ;

	//char buffer[256];
	//sprintf( buffer, "%d %d %d %d",w,h,GetSystemMetrics(SM_CXFULLSCREEN),GetSystemMetrics(SM_CYFULLSCREEN));
	//MessageBox(hwnd, buffer,"Info",MB_OK);
	
	//SetWindowPos(ParentWindow, NULL, 0, 0, w, (int)(h/2.), SWP_NOZORDER) ;
	//SetWindowPos(hwnd, NULL, 0, (int)(h/2.)+1, w, h, SWP_NOZORDER) ;
	
	MoveWindow( ParentWindow, 0, 0, w, (int)(h/2.), TRUE );
	MoveWindow( hwnd, 0, (int)(h/2.)+1, w, (int)(h/2.), TRUE );
	}

// Denombre le nombre de fenetre KiTTY
static int nbProgWin = 0 ;
BOOL CALLBACK CountProgWinProc( HWND hwnd, LPARAM lParam ) {
	char buffer[256] ;
	GetClassName( hwnd, buffer, 256 ) ;
	if( !strcmp( buffer, get_param_str("CLASS") ) )	nbProgWin++ ;
	return TRUE ;
	}

int CountProgWin( HWND hwnd ) {
	nbProgWin=0 ;
	EnumWindows( CountProgWinProc, 0 ) ;
	return nbProgWin ;
	}

// Redimensionne toutes les fenêtres
BOOL CALLBACK ResizeAllWindowsProc( HWND hwnd, LPARAM lParam ) {
	char buffer[256] ;
	int cxScreen, cyScreen ;
	//cxScreen = GetSystemMetrics (SM_CXFULLSCREEN);
	//cyScreen = GetSystemMetrics (SM_CYFULLSCREEN);
	cxScreen = GetSystemMetrics (SM_CXSCREEN);
	cyScreen = GetSystemMetrics (SM_CYSCREEN)-40 ;
	int nbCol = floor(sqrt(nbProgWin));
	int nbLig = ceil(1.0*nbProgWin/nbCol) ;
	int *i=(int*)lParam;
	int larg, haut;
	larg = (int)(cxScreen/nbCol) ;
	haut = (int)(cyScreen/nbLig) ;
	
	GetClassName( hwnd, buffer, 256 ) ;
	if( !strcmp( buffer, get_param_str("CLASS") ) ) {
		int num=nbProgWin-*i;
		int px=0, py=0 ;
		if( num>0 ) {
			int j ;
			for( j=1 ; j<=num ; j++ ) {
				px++ ;
				if( px>= nbCol ) { py++ ; px=0 ; }
			}
		}
//char b[256];sprintf(b,"nbProgWin=%d nbCol=%d nbLig=%d num=%d px=%d py=%d",nbProgWin,nbCol,nbLig,num,px,py);MessageBox(NULL,b,"info",MB_OK);
		MoveWindow( hwnd, px*larg, py*haut, larg, haut, TRUE ) ;
		SetForegroundWindow(hwnd); ;
		*i=*i-1;
	}
	return TRUE;
}
void ResizeAllWindows( HWND hwnd ) {
	int i;
	i=CountProgWin( hwnd ) ;
	EnumWindows( ResizeAllWindowsProc,(LPARAM)&i) ;
}

// Cascade toute les fenêtres KiTTY
BOOL CALLBACK CascadeAllWindowsProc( HWND hwnd, LPARAM lParam ) {
	char buffer[256] ;
	int cxScreen, cyScreen ;
	cxScreen = GetSystemMetrics (SM_CXSCREEN);
	cyScreen = GetSystemMetrics (SM_CYSCREEN)-40 ;
	int *i=(int*)lParam;
	int larg, haut, dec=30;
	larg = (int)(cxScreen-dec*nbProgWin) ;
	haut = (int)(cyScreen-dec*nbProgWin) ;
	
	GetClassName( hwnd, buffer, 256 ) ;
	if( !strcmp( buffer, get_param_str("CLASS") ) ) {
		int num=nbProgWin-*i;
		MoveWindow( hwnd, num*dec, num*dec, larg, haut, TRUE ) ;
		SetForegroundWindow(hwnd); ;
		*i=*i-1;
	}
	return TRUE;
}
void CascadeAllWindows( HWND hwnd ) {
	int i;
	i=CountProgWin( hwnd ) ;
	EnumWindows( CascadeAllWindowsProc,(LPARAM)&i) ;
}
