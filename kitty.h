#ifndef KITTY_H
#define KITTY_H
#include <math.h>
#include <sys/types.h>
#include <process.h>
#include <time.h>

// Handle sur la fenetre principale
extern HWND MainHwnd ;



/*****************************************************
** DEFINITION DES VARIABLES STATIQUE DE kitty.c
** ET DE LEUR FONCTIONS D'ACCES ET DE MODIFICATION
*****************************************************/
// Flag pour repasser en mode Putty basic
extern int PuttyFlag ;


// Flag pour retourner à la Config Box en fin d'execution
// extern int ConfigBoxNoExitFlag ;
int GetConfigBoxNoExitFlag(void) ;

// Flag pour inhiber la gestion du CTRL+TAB
int GetCtrlTabFlag(void) ;
void SetCtrlTabFlag( const int flag ) ;

#ifdef RECONNECTPORT
// Flag pour inhiber le mécanisme de reconnexion automatique
int GetAutoreconnectFlag( void ) ;
void SetAutoreconnectFlag( const int flag ) ;
// Delai avant de tenter une reconnexion automatique
int GetReconnectDelay(void) ;
#endif

// Delai avant d'envoyer le password et d'envoyer vers le tray (automatiquement à la connexion) (en milliseconde)
extern int init_delay ;

// Delai entre chaque ligne de la commande automatique (en milliseconde)
extern int autocommand_delay ;

// Delai entre chaque caracteres d'une commande (en millisecondes)
extern int between_char_delay ;

// Delai entre deux lignes d'une meme commande et entre deux raccourcis \x \k
extern int internal_delay ;

// Nom de la classe de l'application
extern char KiTTYClassName[128] ;

// Flag pour afficher l'image de fond
extern int BackgroundImageFlag ;

// Flag pour imposer le passage en majuscule
// extern int CapsLockFlag ;
int GetCapsLockFlag(void) ;
void SetCapsLockFlag( const int flag ) ;

// Flag pour gerer la presence de la barre de titre
// extern int TitleBarFlag ;
int GetTitleBarFlag(void) ;
void SetTitleBarFlag( const int flag ) ;

// Flag pour passer en mode visualiseur d'images
// extern int ImageViewerFlag ;
int GetImageViewerFlag(void) ;
void SetImageViewerFlag( const int flag ) ;

// Duree (en secondes) pour switcher l'image de fond d'ecran (<=0 pas de slide)
extern int ImageSlideDelay ;

// Nombre de clignotements max de l'icone dans le systeme tray lors de la reception d'un BELL
extern int MaxBlinkingTime ;

// Flag pour l'affichage de la taille de la fenetre
// extern int SizeFlag ;
int GetSizeFlag(void) ;
void SetSizeFlag( const int flag ) ;

// Flag pour la protection contre les saisies malheureuses
// extern int ProtectFlag ; 
int GetProtectFlag(void) ;
void SetProtectFlag( const int flag ) ;

// Flag de definition de la visibilite d'une fenetres
// extern int VisibleFlag ;
int GetVisibleFlag(void) ;
void SetVisibleFlag( const int flag ) ;

// Gestion du script file au lancement
extern char * ScriptFileContent ;

// Flag pour inhiber les raccourcis clavier
// extern int ShortcutsFlag ;
int GetShortcutsFlag(void) ;
void SetShortcutsFlag( const int flag ) ;

// Flag pour inhiber les raccourcis souris
// extern int MouseShortcutsFlag ;
int GetMouseShortcutsFlag(void) ;
void SetMouseShortcutsFlag( const int flag ) ;

// Stuff for drag-n-drop
#ifndef TIMER_DND
#define TIMER_DND 8777
#endif
extern HDROP hDropInf;
void recupNomFichierDragDrop(HWND hwnd, HDROP* leDrop) ;

// Pointeur sur la commande autocommand
extern char * AutoCommand ;

// Contenu d'un script a envoyer à l'ecran
extern char * ScriptCommand ;

// Pointeur sur la commande a passer ligne a ligne
extern char * PasteCommand ;
int GetPasteCommandFlag(void) ;

// Flag de gestion de la fonction hyperlink
extern int HyperlinkFlag ;
int GetHyperlinkFlag(void) ;
void SetHyperlinkFlag( const int flag ) ;

// Flag de gestion de la fonction "rutty" (script automatique)
//extern int RuTTYFlag ;
int GetRuTTYFlag(void) ;
void SetRuTTYFlag( const int flag ) ;

// Flag pour le fonctionnement en mode "portable" (gestion par fichiers), defini dans kitty_commun.c
extern int IniFileFlag ;

// Flag permettant la gestion de l'arborscence (dossier=folder) dans le cas d'un savemode=dir, defini dans kitty_commun.c
extern int DirectoryBrowseFlag ;

// Renvoi automatiquement dans le tray (pour les tunnel), fonctionne avec le l'option -send-to-tray
//extern int AutoSendToTray ;
int GetAutoSendToTray( void ) ;
void SetAutoSendToTray( const int flag ) ;

// Flag de gestion de la Transparence
// extern int TransparencyFlag ;
int GetTransparencyFlag(void) ;
void SetTransparencyFlag( const int flag ) ;

// Flag pour inhiber les fonctions ZMODEM
// extern int ZModemFlag ;
int GetZModemFlag(void) ;
void SetZModemFlag( const int flag ) ;

// Flag pour ne pas creer les fichiers kitty.ini et kitty.sav
// extern int NoKittyFileFlag ;
int GetNoKittyFileFlag(void) ;
void SetNoKittyFileFlag( const int flag ) ;

// Hauteur de la boite de configuration
// extern int ConfigBoxHeight ;
int GetConfigBoxHeight(void) ;
void SetConfigBoxHeight( const int num ) ;

// Hauteur de la fenetre de la boite de configuration (0=valeur par defaut)
// static int ConfigBoxWindowHeight = 0 ;
int GetConfigBoxWindowHeight(void) ;
void SetConfigBoxWindowHeight( const int num ) ;

// Hauteur de la fenetre pour la fonction winrol
// extern int WinHeight ;
int GetWinHeight(void) ;
void SetWinHeight( const int num ) ;
// Flag pour inhiber le Winrol
// extern int WinrolFlag = 1 
int GetWinrolFlag(void) ;
void SetWinrolFlag( const int num ) ;

// Flag permettant de desactiver la sauvegarde automatique des informations de connexion (user/password) à la connexion SSH
// extern int UserPassSSHNoSave ; ==> Defini dans kitty_commun.c
int GetUserPassSSHNoSave(void) ;
void SetUserPassSSHNoSave( const int flag ) ;

// Flag pour inhiber le filtre sur la liste des sessions de la boite de configuration
// extern int SessionFilterFlag ;
// [ConfigBox] filter=yes
int GetSessionFilterFlag(void) ;
void SetSessionFilterFlag( const int flag ) ;

// Flag pour inhiber le comportement ou toutes les sessions appartiennent au folder defaut
// [ConfigBox] default=yes
int GetSessionsInDefaultFlag(void) ;
void SetSessionsInDefaultFlag( const int flag ) ;

// Flag pour inhiber la création automatique de la session Default Settings
// [ConfigBox] defaultsettings=yes
int GetDefaultSettingsFlag(void) ;
void SetDefaultSettingsFlag( const int flag ) ;

#ifdef ADBPORT
// Flag pour inhiber le support d'ADB
int GetADBFlag(void) ;
void SetADBFlag( const int flag ) ;
#endif

// Chemin vers le programme cthelper.exe
extern char * CtHelperPath ;

// Chemin vers le programme WinSCP
extern char * WinSCPPath ;

// Chemin vers le programme pscp.exe
extern char * PSCPPath  ;

// Options pour le programme pscp.exe
extern char PSCPOptions[]  ;

// Repertoire de lancement
extern char InitialDirectory[4096] ;

// Répertoire de sauvegarde de la configuration (savemode=dir)
extern char * ConfigDirectory ;

// Positionne un flag permettant de determiner si on est connecte
extern int backend_connected ;

/* Flag pour interdire l'ouverture de boite configuration */
extern int force_reconf ; 

// Compteur pour l'envoi de anti-idle
extern int AntiIdleCount ;
extern int AntiIdleCountMax ;
extern char AntiIdleStr[128] ;

NOTIFYICONDATA TrayIcone ;
#ifndef MYWM_NOTIFYICON
#define MYWM_NOTIFYICON		(WM_USER+3)
#endif

// Flag pour permettre la definition d'icone de connexion
// extern int IconeFlag ;
int GetIconeFlag(void) ;
void SetIconeFlag( const int flag ) ;

// Nombre d'icones differentes (identifiant commence a 1 dans le fichier .rc)
// extern int NumberOfIcons ;
int GetNumberOfIcons(void) ;
void SetNumberOfIcons( const int flag ) ;

// extern int IconeNum ;
int GetIconeNum(void) ;
void SetIconeNum( const int num ) ;

// La librairie dans laquelle chercher les icones (fichier defini dans kitty.ini, sinon kitty.dll s'il existe, sinon kitty.exe)
// extern HINSTANCE hInstIcons ;
HINSTANCE GethInstIcons(void) ;
void SethInstIcons( const HINSTANCE h ) ;

extern int debug_flag ;

#ifdef ZMODEMPORT
#ifndef IDM_XYZSTART
#define IDM_XYZSTART  0x0810
#endif
#ifndef IDM_XYZUPLOAD
#define IDM_XYZUPLOAD 0x0820
#endif
#ifndef IDM_XYZABORT
#define IDM_XYZABORT  0x0830
#endif
int xyz_Process(Backend *back, void *backhandle, Terminal *term) ;
void xyz_ReceiveInit(Terminal *term) ;
void xyz_StartSending(Terminal *term) ;
void xyz_Cancel(Terminal *term) ;
void xyz_updateMenuItems(Terminal *term) ;
#endif

extern int PORT ;

// Declaration de prototypes de fonction
void InitFolderList( void ) ;
void SaveFolderList( void ) ;
void InfoBoxSetText( HWND hwnd, char * st ) ;
void InfoBoxClose( HWND hwnd ); 
void routine_server( void * st ) ;
void routine_SaveRegistryKey( void * st ) ;
void SetNewIcon( HWND hwnd, char * iconefile, int icone, const int mode ) ;
int WINAPI Notepad_WinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow) ;
void InitWinMain( void ) ;
char * getcwd (char * buf, size_t size);
int chdir(const char *path); 
char * InputBox( HINSTANCE hInstance, HWND hwnd ) ;
char *itoa(int value, char *string, int radix);
void GetAndSendLinePassword( HWND hwnd ) ;
int unlink(const char *pathname);
void RunScriptFile( HWND hwnd, const char * filename ) ;
void InfoBoxSetText( HWND hwnd, char * st ) ;
void ReadInitScript( const char * filename ) ;
int ReadParameter( const char * key, const char * name, char * value ) ;
int WriteParameter( const char * key, const char * name, char * value ) ;
int DelParameter( const char * key, const char * name ) ;
void GetSessionFolderName( const char * session_in, char * folder ) ;
int MakeDirTree( const char * Directory, const char * s, const char * sd ) ;
int ManageShortcuts( HWND hwnd, int key_num, int shift_flag, int control_flag, int alt_flag, int altgr_flag, int win_flag ) ;
void mungestr(const char *in, char *out);
void unmungestr(const char *in, char *out, int outlen);
void print_log( const char *fmt, ...) ;
char * SetInitialSessPath( void ) ;
char * SetSessPath( const char * dec ) ;
void CleanFolderName( char * folder ) ;
void SetInitCurrentFolder( const char * name ) ;
int print_event_log( FILE * fp, int i ) ;
void set_sshver( const char * vers ) ;
int ResizeWinList( HWND hwnd, int width, int height ) ;
int SendCommandAllWindows( HWND hwnd, char * cmd ) ;
int decode64 (char *buffer) ;
void RunCommand( HWND hwnd, char * cmd ) ;
void timestamp_change_filename( void ) ;
int InternalCommand( HWND hwnd, char * st ) ;
// Convertit la base de registre en repertoire pour le mode savemode=dir
int Convert2Dir( const char * Directory ) ;
// Convertit une sauvegarde en mode savemode=dir vers la base de registre
void ConvertDir2Reg( const char * Directory, HKEY hKey, char * path ) ;
int Convert2Reg( const char * Directory ) ;
void load_open_settings_forced(char *filename, Conf *conf) ;
void save_open_settings_forced(char *filename, Conf *conf) ;
int SwitchCryptFlag( void ) ;
void CreateDefaultIniFile( void ) ;
void InitSpecialMenu( HMENU m, const char * folder, const char * sessionname ) ;
// Recupere une entree d'une session ( retourne 1 si existe )
int GetSessionField( const char * session_in, const char * folder_in, const char * field, char * result ) ;
// Sauve les coordonnees de la fenetre
void SaveWindowCoord( Conf * conf ) ;
// Permet de recuperer les sessions KiTTY dans PuTTY  (PUTTY_REG_POS)
void RepliqueToPuTTY( LPCTSTR Key ) ;
// Decompte le nombre de fenetre de la meme classe que KiTTY
int WindowsCount( HWND hwnd ) ;
HWND InfoBox( HINSTANCE hInstance, HWND hwnd ) ;
// Renomme une Cle de registre
void RegRenameTree( HWND hdlg, HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR lpDestKey ) ;
void DelRegistryKey( void ) ;
void MASKPASS( char * password ) ;
void RenewPassword( Conf *conf ) ;
// Gere l'envoi dans le System Tray
int ManageToTray( HWND hwnd ) ;
void RefreshBackground( HWND hwnd ) ;
void SendAutoCommand( HWND hwnd, const char * cmd ) ;
int NextBgImage( HWND hwnd ) ;
int PreviousBgImage( HWND hwnd ) ;
void ManageSpecialCommand( HWND hwnd, int menunum ) ;
int fileno(FILE *stream) ;
// Sauvegarde de la cle de registre
void SaveRegistryKeyEx( HKEY hMainKey, LPCTSTR lpSubKey, const char * filename ) ;
void ManageProtect( HWND hwnd, char * title ) ;
// Gere l'option always visible
void ManageVisible( HWND hwnd ) ;
// Sauvegarde de la cle de registre
void SaveRegistryKeyEx( HKEY hMainKey, LPCTSTR lpSubKey, const char * filename ) ;
void SaveRegistryKey( void ) ;
void ManageWinrol( HWND hwnd, int resize_action ) ;
void resize( int height, int width ) ;
void OpenAndSendScriptFile( HWND hwnd ) ;
void SaveCurrentSetting( HWND hwnd ) ;
void SendFile( HWND hwnd ) ;
void StartWinSCP( HWND hwnd, char * directory, char * host, char * user ) ;
void StartNewSession( HWND hwnd, char * directory, char * host, char * user ) ;
void urlhack_launch_url(const char* app, const char *url) ;
int ShowPortfwd( HWND hwnd, Conf * conf ) ;
void OnDropFiles(HWND hwnd, HDROP hDropInfo) ;
// Affiche un menu dans le systeme Tray
void DisplaySystemTrayMenu( HWND hwnd ) ;
// shift+bouton droit => paste ameliore pour serveur "lent"
// Le paste utilise la methode "autocommand"
void SetPasteCommand( void ) ;
// Recupere les coordonnees de la fenetre
void GetWindowCoord( HWND hwnd ) ;
int ManageLocalCmd( HWND hwnd, char * cmd ) ;
// Gestion du script au lancement
void ManageInitScript( const char * input_str, const int len ) ;
void SetNewIcon( HWND hwnd, char * iconefile, int icone, const int mode ) ;
void GotoInitialDirectory( void ) ;
void GotoConfigDirectory( void ) ;

// Prototype de function de kitty_savedump.c
void addkeypressed( UINT message, WPARAM wParam, LPARAM lParam, int shift_flag, int control_flag, int alt_flag, int altgr_flag, int win_flag ) ;

char * get_param_str( const char * val ) ;


void NegativeColours(HWND hwnd) ;
void BlackOnWhiteColours(HWND hwnd) ;
void ChangeFontSize(HWND hwnd, int dec) ;

#ifdef LAUNCHERPORT
void InitLauncherRegistry( void ) ;
#endif
#ifdef CYGTERMPORT
void cygterm_set_flag( int flag ) ;
int cygterm_get_flag( void ) ;
#endif

int getpid(void) ;

// Definition de la section du fichier de configuration
#if (defined PERSOPORT) && (!defined FDJ)

#ifndef INIT_SECTION
#define INIT_SECTION "KiTTY"
#endif
#ifndef DEFAULT_INIT_FILE
#define DEFAULT_INIT_FILE "kitty.ini"
#endif
#ifndef DEFAULT_SAV_FILE
#define DEFAULT_SAV_FILE "kitty.sav"
#endif
#ifndef DEFAULT_EXE_FILE
#define DEFAULT_EXE_FILE "kitty.exe"
#endif

#else

#ifndef INIT_SECTION
#define INIT_SECTION "PuTTY"
#endif
#ifndef DEFAULT_INIT_FILE
#define DEFAULT_INIT_FILE "putty.ini"
#endif
#ifndef DEFAULT_SAV_FILE
#define DEFAULT_SAV_FILE "putty.sav"
#endif
#ifndef DEFAULT_EXE_FILE
#define DEFAULT_EXE_FILE "putty.exe"
#endif

#endif

#ifndef SI_INIT
#define SI_INIT 0
#endif
#ifndef SI_NEXT
#define SI_NEXT 1
#endif
#ifndef SI_RANDOM
#define SI_RANDOM 2
#endif


#ifndef TIMER_INIT
#define TIMER_INIT 8701
#endif

#ifndef TIMER_AUTOCOMMAND
#define TIMER_AUTOCOMMAND 8702
#endif

#ifndef TIMER_SLIDEBG
#if (defined IMAGEPORT) && (!defined FDJ)
#define TIMER_SLIDEBG 8703
#endif
#endif
#ifndef TIMER_REDRAW
#define TIMER_REDRAW 8704
#endif
#ifndef TIMER_AUTOPASTE
#define TIMER_AUTOPASTE 8705
#endif
#ifndef TIMER_BLINKTRAYICON
#define TIMER_BLINKTRAYICON 8706
#endif
#ifndef TIMER_LOGROTATION
#define TIMER_LOGROTATION 8707
#endif
#ifndef TIMER_ANTIIDLE
#define TIMER_ANTIIDLE 8708
#endif
#ifndef TIMER_RECONNECT
#define TIMER_RECONNECT 8709
#endif

#ifdef RECONNECTPORT
extern int backend_first_connected ; /* Variable permettant de savoir qu'on a deja ete connecte */
#endif

#ifndef VISIBLE_NO
#define VISIBLE_NO 0
#endif
#ifndef VISIBLE_YES
#define VISIBLE_YES 1
#endif
#ifndef VISIBLE_TRAY
#define VISIBLE_TRAY -1
#endif


#ifndef IDM_QUIT
#define IDM_QUIT 0x0100
#endif
#ifndef IDM_VISIBLE
#define IDM_VISIBLE   0x0120
#endif
#ifndef IDM_PROTECT
#define IDM_PROTECT   0x0210
#endif
#ifndef IDM_PRINT
#define IDM_PRINT   0x0220
#endif
#ifndef IDM_TRANSPARUP
#define IDM_TRANSPARUP	0x0230
#endif
#ifndef IDM_TRANSPARDOWN
#define IDM_TRANSPARDOWN	0x0240
#endif
#ifndef IDM_WINROL
#define IDM_WINROL   0x0250
#endif
#ifndef IDM_PSCP
#define IDM_PSCP	0x0260
#endif
#ifndef IDM_WINSCP
#define IDM_WINSCP	0x0270
#endif
#ifndef IDM_TOTRAY
#define IDM_TOTRAY   0x0280
#endif
#ifndef IDM_FROMTRAY
#define IDM_FROMTRAY   0x0290
#endif
#ifndef IDM_SHOWPORTFWD
#define IDM_SHOWPORTFWD	0x0300
#endif
#ifndef IDM_HIDE
#define IDM_HIDE	0x0310
#endif
#ifndef IDM_UNHIDE
#define IDM_UNHIDE	0x0320
#endif
#ifndef IDM_SWITCH_HIDE
#define IDM_SWITCH_HIDE 0x0330
#endif
#ifndef IDM_GONEXT
#define IDM_GONEXT	0x0340
#endif
#ifndef IDM_GOPREVIOUS
#define IDM_GOPREVIOUS	0x0350
#endif
#ifndef IDM_SCRIPTFILE
#define IDM_SCRIPTFILE 0x0360
#endif
#ifndef IDM_RESIZE
#define IDM_RESIZE 0x0370
#endif
#ifndef IDM_REPOS
#define IDM_REPOS 0x0380
#endif
#ifndef IDM_EXPORTSETTINGS
#define IDM_EXPORTSETTINGS 0x0390
#endif

#ifndef IDM_FONTUP
#define IDM_FONTUP 0x0400
#endif
#ifndef IDM_FONTDOWN
#define IDM_FONTDOWN 0x0410
#endif
#ifndef IDM_FONTBLACKANDWHITE
#define IDM_FONTBLACKANDWHITE 0x0420
#endif
#ifndef IDM_FONTNEGATIVE
#define IDM_FONTNEGATIVE 0x0430
#endif

#ifndef IDM_PORTKNOCK
#define IDM_PORTKNOCK	0x0440
#endif
#ifndef IDM_CLEARLOGFILE
#define IDM_CLEARLOGFILE 0x0610
#endif

// Doit etre le dernier
#ifndef IDM_LAUNCHER
#define IDM_LAUNCHER	0x1000
#endif

// USERCMD doit etre la plus grande valeur pour permettre d'avoir autant de raccourcis qu'on le souhaite
#ifndef IDM_USERCMD
#define IDM_USERCMD   0x8000
#endif

// Idem USERCMD
#ifndef IDM_GOHIDE
#define IDM_GOHIDE    0x9000
#endif

#ifndef IDB_OK
#define IDB_OK	1098
#endif


#ifndef SAVEMODE_REG
#define SAVEMODE_REG 0
#endif
#ifndef SAVEMODE_FILE
#define SAVEMODE_FILE 1
#endif
#ifndef SAVEMODE_DIR
#define SAVEMODE_DIR 2
#endif

#ifndef NB_MENU_MAX
#define NB_MENU_MAX 1024
#endif



// Liste de define recupere de WINDOW.C necessaires a kitty.c
#ifndef IDM_ABOUT
#define IDM_ABOUT     0x0150
#endif
#ifndef IDM_RESET
#define IDM_RESET     0x0070
#endif
#ifndef IDM_COPYALL
#define IDM_COPYALL   0x0170
#endif
#ifndef IDM_RESTART
#define IDM_RESTART   0x0040
#endif
#ifndef IDM_DUPSESS
#define IDM_DUPSESS   0x0030
#endif

#endif // KITTY_H
