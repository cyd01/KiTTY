/*
 * Fichier contenant les procedures communes à tous les programmes putty, pscp, psftp, plink, pageant
 */

#include "kitty_commun.h"
#include "kitty_tools.h"

// Flag permettant d'activer l'acces a du code particulier permettant d'avoir plus d'info dans le kitty.dmp
int debug_flag = 0 ;

#ifdef PERSOPORT

#ifndef SAVEMODE_REG
#define SAVEMODE_REG 0
#endif
#ifndef SAVEMODE_FILE
#define SAVEMODE_FILE 1
#endif
#ifndef SAVEMODE_DIR
#define SAVEMODE_DIR 2
#endif

// Flag pour le fonctionnement en mode "portable" (gestion par fichiers)
#ifdef PORTABLE
int IniFileFlag = SAVEMODE_DIR ;
#else
int IniFileFlag = SAVEMODE_REG ;
#endif

// Flag permettant la gestion de l'arborscence (dossier=folder) dans le cas d'un savemode=dir
#ifdef PORTABLE
int DirectoryBrowseFlag = 1 ;
#else
int DirectoryBrowseFlag = 0 ;
#endif

// Flag permettant de sauvegarder automatique les cles SSH des serveurs
static int AutoStoreSSHKeyFlag = 0 ;
int GetAutoStoreSSHKeyFlag(void) { return AutoStoreSSHKeyFlag ; }
void SetAutoStoreSSHKeyFlag( const int flag ) { AutoStoreSSHKeyFlag = flag ; }

// Flag permettant de desactiver la sauvegarde automatique des informations de connexion (user/password) Ã  la connexion SSH
static int UserPassSSHNoSave = 0 ;
int GetUserPassSSHNoSave(void) { return UserPassSSHNoSave ; }
void SetUserPassSSHNoSave( const int flag ) { UserPassSSHNoSave = flag ; }

// Flag permettant de gérer la demande confirmation à l'usage d'une clé privée (1=always; 0=never; 2=based on "comment")
static int AskConfirmationFlag=2 ;
int GetAskConfirmationFlag(void) { return AskConfirmationFlag ; }
void SetAskConfirmationFlag( const int flag ) { AskConfirmationFlag = flag ; }

// Flag pour empêcher l'écriture des fichiers (default settings, jump file list ...)
// [KiTTY] readonly=no
static int ReadOnlyFlag = 0 ;
int GetReadOnlyFlag(void) { return ReadOnlyFlag ; }
void SetReadOnlyFlag( const int flag ) { ReadOnlyFlag = flag ; }

#ifdef ADBPORT
// Flag pour inhiber le support d'ADB
static int ADBFlag = 1 ;
int GetADBFlag(void) { return ADBFlag ; }
void SetADBFlag( const int flag ) { ADBFlag = flag ; }
#endif

// Répertoire de sauvegarde de la configuration (savemode=dir)
char * ConfigDirectory = NULL ;

char * GetConfigDirectory( void ) { return ConfigDirectory ; }

int stricmp(const char *s1, const char *s2) ;
int readINI( const char * filename, const char * section, const char * key, char * pStr) ;
char * SetSessPath( const char * dec ) ;

// Nettoie les noms de folder en remplaçant les "/" par des "\" et les " \ " par des " \"
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

#include <sys/types.h>
#include <dirent.h>
#define MAX_VALUE_NAME 16383
// Supprime une arborescence
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

// Lit un parametre soit dans le fichier de configuration, soit dans le registre
char  * IniFile = NULL ;
char INIT_SECTION[10];
int ReadParameterLight( const char * key, const char * name, char * value ) {
	char buffer[4096] ;
	strcpy( buffer, "" ) ;

	if( GetValueData( HKEY_CURRENT_USER, TEXT(PUTTY_REG_POS), name, buffer ) == NULL ) {
		if( !readINI( IniFile, key, name, buffer ) ) {
			strcpy( buffer, "" ) ;
			}
		}
	strcpy( value, buffer ) ;
	return strcmp( buffer, "" ) ;
	}

/* test if we are in portable mode by looking for putty.ini or kitty.ini in running directory */
int LoadParametersLight( void ) {
	FILE * fp = NULL ;
	int ret = 0 ;
	char buffer[4096] ;

#ifndef FDJ
	if( (getenv("KITTY_INI_FILE")!=NULL) && ((fp = fopen( getenv("KITTY_INI_FILE"), "r" )) != NULL) ) {
		fclose(fp ) ;
		IniFile = (char*)malloc(strlen(getenv("KITTY_INI_FILE"))+1) ; 
		strcpy( IniFile,getenv("KITTY_INI_FILE") ) ;
		strcpy(INIT_SECTION,"KiTTY");
		if( readINI( IniFile, "KiTTY", "savemode", buffer ) ) {
			while( (buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r')
				||(buffer[strlen(buffer)-1]==' ')
				||(buffer[strlen(buffer)-1]=='\t') ) buffer[strlen(buffer)-1]='\0';
			if( !stricmp( buffer, "registry" ) ) IniFileFlag = SAVEMODE_REG ;
			else if( !stricmp( buffer, "file" ) ) IniFileFlag = SAVEMODE_FILE ;
			else if( !stricmp( buffer, "dir" ) ) { IniFileFlag = SAVEMODE_DIR ; ret = 1 ; }
			}
		if(  IniFileFlag == SAVEMODE_DIR ) {
			if( readINI( IniFile, "KiTTY", "browsedirectory", buffer ) ) { 
				if( !stricmp( buffer, "NO" )&&(IniFileFlag==SAVEMODE_DIR) ) DirectoryBrowseFlag = 0 ; 
				else DirectoryBrowseFlag = 1 ;
				}
			if( readINI( IniFile, "KiTTY", "configdir", buffer ) ) {
				if( strlen( buffer ) > 0 ) { 
					ConfigDirectory = (char*)malloc( strlen(buffer) + 1 ) ;
					strcpy( ConfigDirectory, buffer ) ;
					}
				}
			}
		else  DirectoryBrowseFlag = 0 ;
	}
	else if( (fp = fopen( "kitty.ini", "r" )) != NULL ) {
		IniFile = (char*)malloc(11) ; strcpy(IniFile,"kitty.ini");
		strcpy(INIT_SECTION,"KiTTY");
		fclose(fp ) ;
		if( readINI( "kitty.ini", "KiTTY", "savemode", buffer ) ) {
			while( (buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r')
				||(buffer[strlen(buffer)-1]==' ')
				||(buffer[strlen(buffer)-1]=='\t') ) buffer[strlen(buffer)-1]='\0';
			if( !stricmp( buffer, "registry" ) ) IniFileFlag = SAVEMODE_REG ;
			else if( !stricmp( buffer, "file" ) ) IniFileFlag = SAVEMODE_FILE ;
			else if( !stricmp( buffer, "dir" ) ) { IniFileFlag = SAVEMODE_DIR ; ret = 1 ; }
			}
		if(  IniFileFlag == SAVEMODE_DIR ) {
			if( readINI( "kitty.ini", "KiTTY", "browsedirectory", buffer ) ) { 
				if( !stricmp( buffer, "NO" )&&(IniFileFlag==SAVEMODE_DIR) ) DirectoryBrowseFlag = 0 ; 
				else DirectoryBrowseFlag = 1 ;
				}
			if( readINI( "kitty.ini", "KiTTY", "configdir", buffer ) ) { 
				if( strlen( buffer ) > 0 ) { 
					ConfigDirectory = (char*)malloc( strlen(buffer) + 1 ) ;
					strcpy( ConfigDirectory, buffer ) ;
					}
				}
			}
		else  DirectoryBrowseFlag = 0 ;
		}
	else 
#endif
	if( (fp = fopen( "putty.ini", "r" )) != NULL ) {
		IniFile = (char*)malloc(11) ; strcpy(IniFile,"putty.ini");
		strcpy(INIT_SECTION,"PuTTY");
		fclose(fp ) ;
		if( readINI( "putty.ini", "PuTTY", "savemode", buffer ) ) {
			while( (buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r')
				||(buffer[strlen(buffer)-1]==' ')
				||(buffer[strlen(buffer)-1]=='\t') ) buffer[strlen(buffer)-1]='\0';
			if( !stricmp( buffer, "registry" ) ) IniFileFlag = SAVEMODE_REG ;
			else if( !stricmp( buffer, "file" ) ) IniFileFlag = SAVEMODE_FILE ;
			else if( !stricmp( buffer, "dir" ) ) { IniFileFlag = SAVEMODE_DIR ; DirectoryBrowseFlag = 1 ; ret = 1 ; }
			}
		if(  IniFileFlag == SAVEMODE_DIR ) {
			if( readINI( "putty.ini", "PuTTY", "browsedirectory", buffer ) ) {
				if( !stricmp( buffer, "NO" )&&(IniFileFlag==SAVEMODE_DIR) ) DirectoryBrowseFlag = 0 ; 
				else DirectoryBrowseFlag = 1 ;
				}
			if( readINI( "putty.ini", "PuTTY", "configdir", buffer ) ) { 
				if( strlen( buffer ) > 0 ) { 
					ConfigDirectory = (char*)malloc( strlen(buffer) + 1 ) ;
					strcpy( ConfigDirectory, buffer ) ;
					}
				}
			}
		else  DirectoryBrowseFlag = 0 ;
		}
	else {
#ifndef FDJ
		sprintf( buffer, "%s/KiTTY/kitty.ini", getenv("APPDATA") );
		if( (fp = fopen( buffer, "r" )) != NULL ) {
			IniFile = (char*)malloc(strlen(buffer)+1) ; 
			strcpy(IniFile,buffer);
			strcpy(INIT_SECTION,"KiTTY");
			fclose(fp);
		} else {
#endif
		sprintf( buffer, "%s/PuTTY/putty.ini", getenv("APPDATA") );
		if( (fp = fopen( buffer, "r" )) != NULL ) {
			IniFile = (char*)malloc(strlen(buffer)+1) ; 
			strcpy(IniFile,buffer);
			strcpy(INIT_SECTION,"PuTTY");
			fclose(fp);
		} 
#ifndef FDJ
		}
#endif
	}
	
	if( ReadParameterLight( INIT_SECTION, "autostoresshkey", buffer ) ) { if( !stricmp( buffer, "YES" ) ) SetAutoStoreSSHKeyFlag( 1 ) ; }
	if( ReadParameterLight( "Agent", "messageonkeyusage", buffer ) ) { if( !stricmp( buffer, "YES" ) ) SetShowBalloonOnKeyUsage() ; }
	if( ReadParameterLight( "Agent", "askconfirmation", buffer ) ) { 
		if( !stricmp( buffer, "YES" ) ) SetAskConfirmationFlag(1) ; 
		if( !stricmp( buffer, "NO" ) ) SetAskConfirmationFlag(0) ;
		if( !stricmp( buffer, "AUTO" ) ) SetAskConfirmationFlag(2) ;
	}
	
	return ret ;
	}

// Positionne un flag permettant de determiner si on est connecte
int backend_connected = 0 ;

#ifdef RECONNECTPORT
int backend_first_connected = 0 ; 
void SetSSHConnected( int flag ) { 
	backend_connected = flag ; 
	if( flag ) backend_first_connected = 1 ; 
	}
#else
void SetSSHConnected( int flag ) { 
	backend_connected = flag ; 
	}
#endif

PVOID SecureZeroMemory( PVOID ptr, SIZE_T cnt) { return memset( ptr, 0, cnt ) ; }

// Fonction permettant de changer le statut du stockage automatique des ssh host keys
void SetAutoStoreSSHKey( void ) {
	AutoStoreSSHKeyFlag = 1 ;
	}
	
/* Fonction permettant de changer le statut de la generation d'un affichage de balloon dans le system tray lorsqu'une clé privée est utilisée */
static int PrintBalloonOnKeyUsageFlag = 0 ;
void SetShowBalloonOnKeyUsage( void ) {
	PrintBalloonOnKeyUsageFlag = 1 ;
}
int GetShowBalloonOnKeyUsage( void ) { return PrintBalloonOnKeyUsageFlag ; } 
	
/* Fonction permettant d'affiche un message sur l'icone dans le tray */
char *strncpy(  
   char *strDest,  
   const char * strSource,  
   size_t count   
);   
int ShowBalloonTip( NOTIFYICONDATA tnid, TCHAR  title[], TCHAR msg[] ) {
	if( PrintBalloonOnKeyUsageFlag==0 ) return 0 ;
	BOOL res;
	//NOTIFYICONDATA tnid;
	//tnid.cbSize = sizeof(NOTIFYICONDATA) ;
	//tnid.hWnd = hwnd ;
	tnid.uFlags = NIF_INFO ;
	tnid.dwInfoFlags = NIIF_INFO ;
	tnid.uTimeout = 3000 ; /*timeout*/
	strncpy( tnid.szInfo, msg, sizeof( tnid.szInfo ) );
	strncpy( tnid.szInfoTitle, title, sizeof( tnid.szInfoTitle ) );
	res = Shell_NotifyIcon(NIM_MODIFY, &tnid);
	Sleep(3000); 
	tnid.szInfo[0] = 0 ;
	res = Shell_NotifyIcon(NIM_MODIFY, &tnid);
	return res ;	
}
#endif
