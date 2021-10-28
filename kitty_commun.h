#ifndef KITTY_COMMUN
#define KITTY_COMMUN

#include <stdlib.h>
#include <stdio.h>
#include "putty.h"

#include "winstuff.h"
#include <windows.h>

// Flag permettant d'activer l'acces a du code particulier permettant d'avoir plus d'info dans le kitty.dmp
extern int debug_flag ;

// Flag pour repasser en mode Putty basic
int GetPuttyFlag(void) ;
void SetPuttyFlag( const int flag ) ;

// Flag pour le fonctionnement en mode "portable" (gestion par fichiers)
int GetIniFileFlag(void) ;
void SetIniFileFlag( const int flag ) ;
void SwitchIniFileFlag(void) ;

// Flag permettant de sauvegarder automatique les cles SSH des serveurs
// extern int AutoStoreSSHKeyFlag  ;
int GetAutoStoreSSHKeyFlag(void) ;
void SetAutoStoreSSHKeyFlag( const int flag ) ;

// Flag permettant de desactiver la sauvegarde automatique des informations de connexion (user/password) Ã  la connexion SSH
// extern int UserPassSSHNoSave ;
int GetUserPassSSHNoSave(void) ;
void SetUserPassSSHNoSave( const int flag ) ;

// Flag permettant de gérer la demande confirmation à l'usage d'une clé privée (1=always; 0=never; 2=based on "comment")
// extern int AskConfirmationFlag=2 ;
int GetAskConfirmationFlag(void) ;
void SetAskConfirmationFlag( const int flag ) ;

// Flag pour empêcher l'écriture des fichiers (default settings, jump file list ...)
// [KiTTY] readonly=no
int GetReadOnlyFlag(void) ;
void SetReadOnlyFlag( const int flag ) ;

// Flag pour gérer le "mélange" des clés dans pageant
int GetScrumbleKeyFlag(void) ;
void SetScrumbleKeyFlag( const int flag ) ;

// Flag pour afficher l'image de fond
//extern int BackgroundImageFlag ;
int GetBackgroundImageFlag(void) ;
void SetBackgroundImageFlag( const int flag ) ;

// Pour supprimer le salt dans la cryptography: issue:https://github.com/cyd01/KiTTY/issues/113
// extern int RandomActiveFlag ;
int GetRandomActiveFlag() ;
void SetRandomActiveFlag( const int flag ) ;

// Pour supprimer le sel dans le cryptage du mot de passe
int GetCryptSaltFlag() ;
void SetCryptSaltFlag( int flag ) ;

#ifdef MOD_ADB
// Flag pour inhiber le support d'ADB
int GetADBFlag(void) ;
void SetADBFlag( const int flag ) ;
#endif

#ifdef MOD_ZMODEM
// Flag pour inhiber les fonctions ZMODEM
int GetZModemFlag(void) ;
void SetZModemFlag( const int flag ) ;
#endif

// Répertoire de sauvegarde de la configuration (savemode=dir)
extern char * ConfigDirectory ;

char * GetConfigDirectory( void ) ;

int stricmp(const char *s1, const char *s2) ;
char * GetValueData(HKEY hkTopKey, char * lpSubKey, const char * lpValueName, char * rValue) ;
int readINI( const char * filename, const char * section, const char * key, char * pStr) ;
char * SetSessPath( const char * dec ) ;

// Nettoie les noms de folder en remplaçant les "/" par des "\" et les " \ " par des " \"
void CleanFolderName( char * folder ) ;

// Supprime une arborescence
void DelDir( const char * directory ) ;

// Lit un parametre soit dans le fichier de configuration, soit dans le registre
int ReadParameterLight( const char * key, const char * name, char * value ) ;

/* test if we are in portable mode by looking for putty.ini or kitty.ini in running directory */
int LoadParametersLight( void ) ;

// Positionne un flag permettant de determiner si on est connecte
extern int is_backend_connected ;
#ifdef MOD_RECONNECT
extern int is_backend_first_connected ;
#endif

void SetSSHConnected( int flag ) ;

//PVOID WINAPI SecureZeroMemory( PVOID ptr, SIZE_T cnt) ;

// Fonction permettant de changer le statut du stockage automatique des ssh host keys
void SetAutoStoreSSHKey( void ) ;

/* Fonctions permettant de changer ou d'obtenir le statut de la generation d'un affichage de balloon dans le system tray lorsqu'une clé privée est utilisée */
void SetShowBalloonOnKeyUsage( void ) ;
int GetShowBalloonOnKeyUsage( void ) ;

/* Fonction permettant d'affiche un message sur l'icone dans le tray */
int ShowBalloonTip( NOTIFYICONDATA tnid, TCHAR  title[], TCHAR msg[] ) ;

// Fonctions permettant de formatter les chaînes de caractères avec %XY	
void mungestr( const char *in, char *out ) ;
void unmungestr( const char *in, char *out, int outlen ) ;

// Fonctions de gestion du mot de passe
void MASKPASS( const int mode, char * password ) ;
void GetPasswordInConfig( char * p ) ;
int IsPasswordInConf(void) ;
void CleanPassword( char * p ) ;

int _rmdir(const char *) ;

// Extention pour les fichiers de session en mode portable (peut être ktx)
extern char FileExtension[15] ;

// Répertoire courant pourle mode portable
extern char CurrentFolder[1024] ;

//int DebugAddPassword( const char*fct, const char*pwd ) ;
#endif
