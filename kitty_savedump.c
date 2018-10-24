
static char SaveKeyPressed[4096] = "" ;

void WriteCountUpAndPath(void) ;
void SaveDumpPortableConfig( FILE * fp ) ;
int GetAutoStoreSSHKeyFlag(void) ;
int GetUserPassSSHNoSave(void) ;

// Buffer contenant du texte a ecrire au besoin dans le fichier kitty.dmp
static char * DebugText = NULL ;

void set_debug_text( const char * txt ) {
	if( DebugText!=NULL ) { free( DebugText ) ; DebugText = NULL ; }
	if( txt != NULL ) {
		DebugText = (char*) malloc( strlen(txt)+1 ) ;
		strcpy( DebugText, txt ) ;
		}
	}

void addkeypressed( UINT message, WPARAM wParam, LPARAM lParam, int shift_flag, int control_flag, int alt_flag, int altgr_flag, int win_flag ) {
	char buffer[256], c=' ' ;
	int p ;
	
	if( message==WM_KEYDOWN ) c='v' ; else if( message==WM_KEYUP ) c='^' ;
	
	if( shift_flag ) shift_flag = 1 ;
	if( control_flag ) control_flag = 1 ;
	if( alt_flag ) alt_flag = 1 ;
	if( altgr_flag ) altgr_flag = 1 ;
	if( win_flag ) win_flag = 1 ;
	
	if( wParam=='\r' ) 
		sprintf( buffer, "%d%d%d%d%d %d%c %03d(%02X)/%d (\\r)\n",shift_flag,control_flag,alt_flag,altgr_flag,win_flag, message,c, wParam, wParam, 0 ) ;
	else if( wParam=='\n' )
 		sprintf( buffer, "%d%d%d%d%d %d%c %03d(%02X)/%d (\\n)\n",shift_flag,control_flag,alt_flag,altgr_flag,win_flag, message,c, wParam, wParam, 0 ) ;
	else if( (wParam>=32) && (wParam<=111 ) ) 
		sprintf( buffer, "%d%d%d%d%d %d%c %03d(%02X)/%d (%c)\n",shift_flag,control_flag,alt_flag,altgr_flag,win_flag, message,c, wParam, wParam, 0, wParam ) ;
	else if( (wParam>=VK_F1 /*70 112*/) && (wParam<=VK_F24 /*87 135*/) )
		sprintf( buffer, "%d%d%d%d%d %d%c %03d(%02X)/%d (F%d)\n",shift_flag,control_flag,alt_flag,altgr_flag,win_flag, message,c, wParam, wParam, 0, wParam-VK_F1+1 ) ;
	else
		sprintf( buffer, "%d%d%d%d%d %d%c %03d(%02X)/%d\n",shift_flag,control_flag,alt_flag,altgr_flag,win_flag, message,c, wParam, wParam, 0 ) ;
	
	if( strlen(SaveKeyPressed) > 4000 ) {
		if( (p=poss("\n",SaveKeyPressed)) > 0 ) {
			del( SaveKeyPressed, 1, p );
			}
		}
	strcat( SaveKeyPressed, buffer ) ;
	}

#include <psapi.h>
void PrintProcessNameAndID( DWORD processID, FILE * fp  ) {
	TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");
	DWORD SizeOfImage = 0 ;
	
	// Get a handle to the process.
	HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID );

	// Get the process name.
	if (NULL != hProcess ) {
		HMODULE hMod;
		DWORD cbNeeded;
		MODULEINFO modinfo ;
		if ( EnumProcessModules( hProcess, &hMod, sizeof(hMod), &cbNeeded) ) { 
			GetModuleBaseName( hProcess, hMod, szProcessName, sizeof(szProcessName)/sizeof(TCHAR) ) ; 
			GetModuleInformation( hProcess, hMod, &modinfo, sizeof( modinfo ) ) ;
			SizeOfImage = modinfo.SizeOfImage ;
			}
		}

	// Print the process name and identifier.
	fprintf( fp, TEXT("%05u %u \t%s\n"), (unsigned int)processID, (unsigned int)SizeOfImage, szProcessName ) ;
	CloseHandle( hProcess );
	}
	
void PrintWindowSettings( FILE * fp ) {
	int ret ;
	RECT r ;
	char buffer[MAX_VALUE_NAME] ;
	
	GetOSInfo( buffer ) ;
	fprintf( fp, "OSVersion=%s\n", buffer ) ;
	if( IsWow64() ) { fprintf( fp, "64 bits System\n" ) ; } else { fprintf( fp, "32 bits System\n" ) ; }
	
	ret = GetWindowText( MainHwnd, buffer, MAX_VALUE_NAME ) ; buffer[ret]='\0';
	ret = GetWindowTextLength( MainHwnd ) ;
	fprintf( fp, "Title (length)=%s (%d)\n", buffer, ret ) ;
	if( GetWindowRect( MainHwnd, &r ) ) {
		fprintf( fp, "WindowRect.left=%ld\n", r.left ) ;
		fprintf( fp, "WindowRect.right=%ld\n", r.right ) ;
		fprintf( fp, "WindowRect.top=%ld\n", r.top ) ;
		fprintf( fp, "WindowRect.bottom=%ld\n", r.bottom ) ;
		}
	if( GetClientRect( MainHwnd, &r ) ) {
		fprintf( fp, "ClientRect.left=%ld\n", r.left ) ;
		fprintf( fp, "ClientRect.right=%ld\n", r.right ) ;
		fprintf( fp, "ClientRect.top=%ld\n", r.top ) ;
		fprintf( fp, "ClientRect.bottom=%ld\n", r.bottom ) ;
		}
	
	ret = GetWindowModuleFileName( MainHwnd, buffer, MAX_VALUE_NAME ) ; buffer[ret]='\0';
	fprintf( fp, "WindowModuleFileName=%s\n", buffer ) ;
	
	WINDOWINFO wi ;
	wi.cbSize = sizeof( WINDOWINFO ) ;
	if( GetWindowInfo( MainHwnd, &wi ) ) {
		fprintf( fp, "WindowInfo.cbSize=%lu\n", wi.cbSize ) ;
		fprintf( fp, "WindowInfo.rcWindow.left=%ld\n", wi.rcWindow.left ) ;
		fprintf( fp, "WindowInfo.rcWindow.right=%ld\n", wi.rcWindow.right ) ;
		fprintf( fp, "WindowInfo.rcWindow.top=%ld\n", wi.rcWindow.top ) ;
		fprintf( fp, "WindowInfo.rcWindow.bottom=%ld\n", wi.rcWindow.bottom ) ;
		fprintf( fp, "WindowInfo.rcClient.left=%ld\n", wi.rcWindow.left ) ;
		fprintf( fp, "WindowInfo.rcClient.right=%ld\n", wi.rcWindow.right ) ;
		fprintf( fp, "WindowInfo.rcClient.top=%ld\n", wi.rcWindow.top ) ;
		fprintf( fp, "WindowInfo.rcClient.bottom=%ld\n", wi.rcWindow.bottom ) ;
		fprintf( fp, "WindowInfo.dwStyle=%lu\n", wi.dwStyle ) ;
		fprintf( fp, "WindowInfo.dwExStyle=%lu\n", wi.dwExStyle ) ;
		fprintf( fp, "WindowInfo.dwWindowStatus=%lu\n", wi.dwWindowStatus ) ;
		fprintf( fp, "WindowInfo.cxWindowBorders=%u\n", wi.cxWindowBorders ) ;
		fprintf( fp, "WindowInfo.cyWindowBorders=%u\n", wi.cyWindowBorders ) ;
		fprintf( fp, "WindowInfo.wCreatorVersion=%d\n", wi.wCreatorVersion ) ;
		}
	
	WINDOWPLACEMENT wp;
	wp.length=sizeof(WINDOWPLACEMENT) ;
	if( GetWindowPlacement( MainHwnd, &wp ) ) {
		fprintf( fp, "WindowPlacement.length=%u\n", wp.length ) ;
		fprintf( fp, "WindowPlacement.flags=%u\n", wp.flags ) ;
		fprintf( fp, "WindowPlacement.showCmd=%u\n", wp.showCmd ) ;
		fprintf( fp, "WindowPlacement.ptMinPosition.x=%ld\n", wp.ptMinPosition.x ) ;
		fprintf( fp, "WindowPlacement.ptMinPosition.y=%ld\n", wp.ptMinPosition.y ) ;
		fprintf( fp, "WindowPlacement.ptMaxPosition.x=%ld\n", wp.ptMaxPosition.x ) ;
		fprintf( fp, "WindowPlacement.ptMaxPosition.y=%ld\n", wp.ptMaxPosition.y ) ;
		fprintf( fp, "WindowPlacement.rcNormalPosition.left=%ld\n", wp.rcNormalPosition.left ) ;
		fprintf( fp, "WindowPlacement.rcNormalPosition.right=%ld\n", wp.rcNormalPosition.right ) ;
		fprintf( fp, "WindowPlacement.rcNormalPosition.top=%ld\n", wp.rcNormalPosition.top ) ;
		fprintf( fp, "WindowPlacement.rcNormalPosition.bottom=%ld\n", wp.rcNormalPosition.bottom ) ;
		}
	
	fprintf( fp, "IsIconic=%d\n", IsIconic( MainHwnd ) ) ;
	fprintf( fp, "IsWindow=%d\n", IsWindow( MainHwnd ) ) ;
	fprintf( fp, "IsWindowUnicode=%d\n", IsWindowUnicode( MainHwnd ) ) ;
	fprintf( fp, "IsWindowVisible=%d\n", IsWindowVisible( MainHwnd ) ) ;
	fprintf( fp, "IsZoomed=%d\n", IsZoomed( MainHwnd ) ) ;

	fprintf( fp, "ScaleX=%d\n", GetDeviceCaps(GetDC(MainHwnd),LOGPIXELSX) ) ;
	fprintf( fp, "ScaleY=%d\n", GetDeviceCaps(GetDC(MainHwnd),LOGPIXELSY) ) ;
	}

DWORD PrintAllProcess( FILE * fp ) {
	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;

	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) ) return 0 ;

	// Calculate how many process identifiers were returned.
	cProcesses = cbNeeded / sizeof(DWORD);

	// Print the name and process identifier for each process.
	printf( "ID    MEM   \tMODULE\n" );
	if( cProcesses > 0 )
	for ( i = 0; i < cProcesses; i++ )
		if( aProcesses[i] != 0 )
			PrintProcessNameAndID( aProcesses[i], fp ) ;
	return cProcesses ;
	}

void SaveDumpListFile( FILE * fp, const char * directory ) {
	DIR *dir ;
	struct dirent *de ;
	char buffer[MAX_VALUE_NAME] ;
	
	/* fprintf( fp, "	===> Listing directory %s\n", directory ) ; */
	if( ( dir=opendir( directory ) ) != NULL ) {
		while( ( de=readdir( dir ) ) != NULL ) {
			if( strcmp(de->d_name,".")&&strcmp(de->d_name,"..") ) {
				sprintf( buffer, "%s\\%s", directory, de->d_name ) ;
				if( GetFileAttributes( buffer ) & FILE_ATTRIBUTE_DIRECTORY ) {  
					strcat( buffer, "\\" ) ; 
					fprintf( fp, "%s\n", buffer ) ;
					SaveDumpListFile( fp, buffer ) ;
				}
				else { fprintf( fp, "%s\n", buffer ) ; }
			}
		}
		closedir( dir ) ;
	}
}

void SaveDumpListConf( FILE *fp, const char *directory ) {
	char buffer[4096], fullpath[MAX_VALUE_NAME] ;
	FILE *fp2 ;
	DIR *dir ;
	struct dirent *de ;
	if( ( dir=opendir( directory ) ) != NULL ) {
		while( ( de=readdir( dir ) ) != NULL ) {
			if( strcmp(de->d_name,".")&&strcmp(de->d_name,"..") ) {
				sprintf( fullpath, "%s\\%s", directory, de->d_name ) ;
				if( GetFileAttributes( fullpath ) & FILE_ATTRIBUTE_DIRECTORY )
					SaveDumpListConf( fp, fullpath ) ;
				else {
					fprintf( fp, "[%s]\n", fullpath ) ;
					if( ( fp2 = fopen( fullpath, "r" ) ) != NULL ) {
						while( fgets( buffer, 4095, fp2 ) != NULL ) fputs( buffer, fp ) ;
						fclose( fp2 ) ;
						}
					fprintf( fp, "\n\n" ) ;
					}
				}
			}
		closedir( dir ) ;
		}
	}

void SaveDumpClipBoard( FILE *fp ) {
	char *pst = NULL ;
	if( term==NULL ) return ;
	term_copyall(term) ;
	if( OpenClipboard(NULL) ) {
		HGLOBAL hglb ;
		if( (hglb = GetClipboardData( CF_TEXT ) ) != NULL ) {
			if( ( pst = GlobalLock( hglb ) ) != NULL ) {
				//fputs( pst, fp ) ;
				fwrite( pst, 1, strlen(pst), fp ) ;
				GlobalUnlock( hglb ) ;
				}
			}
		CloseClipboard();
		}
	}

//#include <unistd.h>
void SaveDumpEnvironment( FILE *fp ) {
	int i = 0 ;
	while( environ[i] ) {
		fprintf( fp, "%s\n", environ[i] ) ;
		i++;
		}
	}
	
void SaveDumpConfig( FILE *fp, Conf * conf ) {
	char *buf=NULL ;
	CountUp();
	fprintf( fp, "MASTER_PASSWORD=%s\n", MASTER_PASSWORD ) ;
	fprintf( fp, "[[PuTTY structure configuration]]\n" ) ;
	fprintf( fp, "sessionname=%s\n", 		conf_get_str(conf,CONF_sessionname) ) ;
	/* Basic options */
	fprintf( fp, "host=%s\n", 			conf_get_str(conf,CONF_host) ) ;
	fprintf( fp, "port=%d\n", 			conf_get_int(conf,CONF_port) ) ;
	fprintf( fp, "protocol=%d\n", 			conf_get_int(conf,CONF_protocol) ) ;
	fprintf( fp, "addressfamily=%d\n",		conf_get_int(conf,CONF_addressfamily) ) ;
	fprintf( fp, "close_on_exit=%d\n",		conf_get_int(conf,CONF_close_on_exit) ) ;
	fprintf( fp, "warn_on_close=%d\n",		conf_get_int(conf,CONF_warn_on_close) ) ;
	fprintf( fp, "ping_interval=%d\n", 		conf_get_int(conf,CONF_ping_interval) ) ;
	fprintf( fp, "tcp_nodelay=%d\n", 		conf_get_int(conf,CONF_tcp_nodelay) ) ;
	fprintf( fp, "tcp_keepalives=%d\n", 		conf_get_int(conf,CONF_tcp_keepalives) ) ;
	/* Proxy options */
	fprintf( fp, "proxy_exclude_list=%s\n",		conf_get_str(conf,CONF_proxy_exclude_list) ) ;
	fprintf( fp, "proxy_dns=%d\n", 			conf_get_int(conf,CONF_proxy_dns) ) ;
	fprintf( fp, "even_proxy_localhost=%d\n",	conf_get_int(conf,CONF_even_proxy_localhost) ) ;
	fprintf( fp, "proxy_type=%d\n",			conf_get_int(conf,CONF_proxy_type) ) ;
	
	fprintf( fp, "proxy_host=%s\n", 		conf_get_str(conf,CONF_proxy_host) ) ;
	fprintf( fp, "proxy_port=%d\n", 		conf_get_int(conf,CONF_proxy_port) ) ;
	fprintf( fp, "proxy_username=%s\n", 		conf_get_str(conf,CONF_proxy_username) ) ;
	fprintf( fp, "proxy_password=%s\n", 		conf_get_str(conf,CONF_proxy_password) ) ;
	fprintf( fp, "proxy_telnet_command=%s\n", 	conf_get_str(conf,CONF_proxy_telnet_command) ) ;
	fprintf( fp, "proxy_log_to_term=%d\n", 		conf_get_int(conf,CONF_proxy_log_to_term) ) ;

	/* PERSOPORT Options */
		// fprintf( fp, "bcdelay=%d\n", 			conf_get_int(conf,CONF_bcdelay) ) ;		// Non present systematiquement
		// fprintf( fp, "initdelay=%d\n",			conf_get_int(conf,CONF_initdelay) ) ;		// Non present systematiquement
	fprintf( fp, "transparencynumber=%d\n", 	conf_get_int(conf,CONF_transparencynumber) ) ;
	fprintf( fp, "sendtotray=%d\n",			conf_get_int(conf,CONF_sendtotray) ) ;
	fprintf( fp, "maximize=%d\n",			conf_get_int(conf,CONF_maximize) ) ;
	fprintf( fp, "icone=%d\n",			conf_get_int(conf,CONF_icone) ) ;
	fprintf( fp, "iconefile=%s\n",			conf_get_filename(conf,CONF_iconefile)->path ) ;
	fprintf( fp, "folder=%s\n", 			conf_get_str(conf,CONF_folder) ) ;
	fprintf( fp, "sftpconnect=%s\n", 		conf_get_str(conf,CONF_sftpconnect) ) ;

	char bufpass[4096] ;
	memcpy( bufpass, conf_get_str(conf,CONF_password), 4095 ) ; bufpass[4095]='\0';
	MASKPASS(bufpass);
	fprintf( fp, "password=%s\n",			bufpass ) ;
	memset(bufpass,0,strlen(bufpass));

	fprintf( fp, "antiidle=%s\n",			conf_get_str(conf,CONF_antiidle) ) ;
	fprintf( fp, "autocommand=%s\n",		conf_get_str(conf,CONF_autocommand) ) ;
	fprintf( fp, "autocommandout=%s\n",		conf_get_str(conf,CONF_autocommandout) ) ;
	fprintf( fp, "scriptfile=%s\n",			conf_get_filename(conf,CONF_scriptfile)->path ) ;
	fprintf( fp, "scriptfilecontent=%s",		conf_get_str(conf,CONF_scriptfilecontent) ) ;
	
	buf=(char*)malloc( strlen(conf_get_str(conf,CONF_scriptfilecontent)) + 20 ) ;
	strcpy( buf, conf_get_str(conf,CONF_scriptfilecontent) ) ;
	long l=decryptstring( buf, MASTER_PASSWORD ) ;
	int i;
	for( i=0; i<l ; i++ ) { if( buf[i]=='\0' ) buf[i]='\n' ; }
	fprintf( fp, " (%s)\n", buf ) ;
	free(buf);
	buf=NULL;

	/* SSH options */
	fprintf( fp, "remote_cmd=%s\n",			conf_get_str(conf,CONF_remote_cmd) ) ;
	//char *remote_cmd_ptr;	       /* might point to a larger command but never for loading/saving */
	//char *remote_cmd_ptr2;	       /* might point to a larger command but never for loading/saving */
	fprintf( fp, "nopty=%d\n",			conf_get_int(conf,CONF_nopty) ) ;
	fprintf( fp, "compression=%d\n",		conf_get_int(conf,CONF_compression) ) ;
	fprintf( fp, "ssh_rekey_time=%d\n",		conf_get_int(conf,CONF_ssh_rekey_time) ) ;
	//int ssh_kexlist[KEX_MAX];
	fprintf( fp, "ssh_rekey_data=%s\n",		conf_get_str(conf,CONF_ssh_rekey_data) ) ;
	fprintf( fp, "tryagent=%d\n",			conf_get_int(conf,CONF_tryagent) ) ;
	fprintf( fp, "agentfwd=%d\n",			conf_get_int(conf,CONF_agentfwd) ) ;
	fprintf( fp, "change_username=%d\n",		conf_get_int(conf,CONF_change_username) ) ;
	//int ssh_cipherlist[CIPHER_MAX];
	fprintf( fp, "keyfile=%s\n",			conf_get_filename(conf,CONF_keyfile)->path ) ;
	fprintf( fp, "sshprot=%d\n",			conf_get_int(conf,CONF_sshprot) ) ;
	fprintf( fp, "ssh2_des_cbc=%d\n",		conf_get_int(conf,CONF_ssh2_des_cbc) ) ;
	fprintf( fp, "ssh_no_userauth=%d\n",		conf_get_int(conf,CONF_ssh_no_userauth) ) ;
	fprintf( fp, "ssh_show_banner=%d\n",		conf_get_int(conf,CONF_ssh_show_banner) ) ;
	fprintf( fp, "try_tis_auth=%d\n",		conf_get_int(conf,CONF_try_tis_auth) ) ;
	fprintf( fp, "try_ki_auth=%d\n",		conf_get_int(conf,CONF_try_ki_auth) ) ;
	fprintf( fp, "try_gssapi_auth=%d\n",		conf_get_int(conf,CONF_try_gssapi_auth) ) ;
	fprintf( fp, "gssapifwd=%d\n",			conf_get_int(conf,CONF_gssapifwd) ) ;
	//fprintf( fp, "ssh_gsslist=%d\n",		conf_get_int(conf,CONF_ssh_gsslist) ) ;
	fprintf( fp, "ssh_gss_custom=%s\n",		conf_get_filename(conf,CONF_ssh_gss_custom)->path ) ;
	//fprintf( fp, "ssh_subsys=%d\n",			conf_get_int(conf,CONF_ssh_subsys) ) ;
	//fprintf( fp, "ssh_subsys2=%d\n",		conf_get_int(conf,CONF_ssh_subsys2) ) ;
	fprintf( fp, "ssh_no_shell=%d\n",		conf_get_int(conf,CONF_ssh_no_shell) ) ;
	//fprintf( fp, "ssh_nc_host=%s\n",		conf_get_str(conf,CONF_ssh_nc_host) ) ;
	//fprintf( fp, "ssh_nc_port=%d\n",		conf_get_int(conf,CONF_ssh_nc_port) ) ;

#ifdef RUTTYPORT
	fprintf( fp, "ScriptFileName=%s\n",		conf_get_filename(conf,CONF_script_filename)->path ) ;
	fprintf( fp, "ScriptMode=%d\n",			conf_get_int(conf,CONF_script_mode) ) ;
	fprintf( fp, "ScriptLineDelay=%d\n",		conf_get_int(conf,CONF_script_line_delay) ) ;
	fprintf( fp, "ScriptCharDelay=%d\n",		conf_get_int(conf,CONF_script_char_delay) ) ;
	fprintf( fp, "ScriptCondLine=%s\n",		conf_get_str(conf,CONF_script_cond_line) ) ;
	fprintf( fp, "ScriptCondUse=%d\n",		conf_get_int(conf,CONF_script_cond_use) ) ;
	fprintf( fp, "ScriptCRLF=%d\n",			conf_get_int(conf,CONF_script_crlf) ) ;
	fprintf( fp, "ScriptEnable=%d\n",		conf_get_int(conf,CONF_script_enable) ) ;
    	fprintf( fp, "ScriptExcept=%d\n",		conf_get_int(conf,CONF_script_except) ) ;
	fprintf( fp, "ScriptTimeout=%d\n",		conf_get_int(conf,CONF_script_timeout) ) ;
	fprintf( fp, "ScriptWait=%s\n",			conf_get_str(conf,CONF_script_waitfor) ) ;
	fprintf( fp, "ScriptHalt=%s\n",			conf_get_str(conf,CONF_script_halton) ) ;
#endif
	/* Telnet options */
	fprintf( fp, "termtype=%s\n",			conf_get_str(conf,CONF_termtype ) ) ;
	fprintf( fp, "termspeed=%s\n",			conf_get_str(conf,CONF_termspeed ) ) ;
	//fprintf( fp, "ttymodes=%s\n",			conf_get_str(conf,CONF_ttymodes ) ) ;
	//fprintf( fp, "environmt=%s\n",			conf_get_str(conf,CONF_environmt ) ) ;
	fprintf( fp, "username=%s\n",			conf_get_str(conf,CONF_username ) ) ;
	fprintf( fp, "localusername=%s\n",		conf_get_str(conf,CONF_localusername ) ) ;
	fprintf( fp, "rfc_environ=%d\n",		conf_get_int(conf,CONF_rfc_environ) ) ;
	fprintf( fp, "passive_telnet=%d\n",		conf_get_int(conf,CONF_passive_telnet) ) ;
	/* Serial port options */
	fprintf( fp, "serline=%s\n", 			conf_get_str(conf,CONF_serline) ) ;
	fprintf( fp, "serspeed=%d\n",			conf_get_int(conf,CONF_serspeed) ) ;
	fprintf( fp, "serdatabits=%d\n",		conf_get_int(conf,CONF_serdatabits) ) ;
	fprintf( fp, "serstopbits=%d\n",		conf_get_int(conf,CONF_serstopbits) ) ;
	fprintf( fp, "serparity=%d\n",			conf_get_int(conf,CONF_serparity) ) ;
	fprintf( fp, "serflow=%d\n",			conf_get_int(conf,CONF_serflow) ) ;
#ifdef IVPORT
	/* Background */
	fprintf( fp, "bg_wallpaper=%d\n", 		conf_get_int(conf, CONF_bg_wallpaper));
	fprintf( fp, "bg_effect=%d\n", 			conf_get_int(conf, CONF_bg_effect));
	fprintf( fp, "wp_file=%s\n", 			conf_get_filename(conf,CONF_wp_file)->path ) ;
	fprintf( fp, "wp_position=%d\n", 		conf_get_int(conf, CONF_wp_position));
	fprintf( fp, "wp_align=%d\n", 			conf_get_int(conf, CONF_wp_align));
	fprintf( fp, "wp_valign=%d\n", 			conf_get_int(conf, CONF_wp_valign));
	fprintf( fp, "wp_moving=%d\n", 			conf_get_int(conf, CONF_wp_moving));
#endif
#ifdef CYGTERMPORT
	/* Cygterm options */
	fprintf( fp, "cygcmd=%s\n", 			conf_get_str(conf,CONF_cygcmd) ) ;
	fprintf( fp, "alt_metabit=%d\n", 		conf_get_int(conf,CONF_alt_metabit) ) ;
#endif
	/* Keyboard options */
	fprintf( fp, "bksp_is_delete=%d\n", 		conf_get_int(conf,CONF_bksp_is_delete) ) ;
	fprintf( fp, "rxvt_homeend=%d\n", 		conf_get_int(conf,CONF_rxvt_homeend) ) ;
	fprintf( fp, "funky_type=%d\n", 		conf_get_int(conf,CONF_funky_type) ) ;
	fprintf( fp, "no_applic_c=%d\n", 		conf_get_int(conf,CONF_no_applic_c) ) ;
	fprintf( fp, "no_applic_k=%d\n", 		conf_get_int(conf,CONF_no_applic_k) ) ;
	fprintf( fp, "no_mouse_rep=%d\n", 		conf_get_int(conf,CONF_no_mouse_rep) ) ;
	fprintf( fp, "no_remote_resize=%d\n", 		conf_get_int(conf,CONF_no_remote_resize) ) ;
	fprintf( fp, "no_alt_screen=%d\n", 		conf_get_int(conf,CONF_no_alt_screen) ) ;
	fprintf( fp, "no_remote_wintitle=%d\n", 	conf_get_int(conf,CONF_no_remote_wintitle) ) ;
	fprintf( fp, "no_remote_clearscroll=%d\n", 	conf_get_int(conf,CONF_no_remote_clearscroll) ) ;
	fprintf( fp, "no_dbackspace=%d\n", 		conf_get_int(conf,CONF_no_dbackspace) ) ;
	fprintf( fp, "no_remote_charset=%d\n", 		conf_get_int(conf,CONF_no_remote_charset) ) ;
	fprintf( fp, "remote_qtitle_action=%d\n", 	conf_get_int(conf,CONF_remote_qtitle_action) ) ;
	fprintf( fp, "app_cursor=%d\n", 		conf_get_int(conf,CONF_app_cursor) ) ;
	fprintf( fp, "app_keypad=%d\n", 		conf_get_int(conf,CONF_app_keypad) ) ;
	fprintf( fp, "nethack_keypad=%d\n", 		conf_get_int(conf,CONF_nethack_keypad) ) ;
	fprintf( fp, "telnet_keyboard=%d\n", 		conf_get_int(conf,CONF_telnet_keyboard) ) ;
	fprintf( fp, "telnet_newline=%d\n", 		conf_get_int(conf,CONF_telnet_newline) ) ;
	fprintf( fp, "alt_f4=%d\n", 			conf_get_int(conf,CONF_alt_f4) ) ;
	fprintf( fp, "alt_space=%d\n", 			conf_get_int(conf,CONF_alt_space) ) ;
	fprintf( fp, "alt_only=%d\n", 			conf_get_int(conf,CONF_alt_only) ) ;
	fprintf( fp, "localecho=%d\n", 			conf_get_int(conf,CONF_localecho) ) ;
	fprintf( fp, "localedit=%d\n", 			conf_get_int(conf,CONF_localedit) ) ;
	fprintf( fp, "alwaysontop=%d\n", 		conf_get_int(conf,CONF_alwaysontop) ) ;
	fprintf( fp, "fullscreenonaltenter=%d\n", 	conf_get_int(conf,CONF_fullscreenonaltenter) ) ;
	fprintf( fp, "scroll_on_key=%d\n", 		conf_get_int(conf,CONF_scroll_on_key) ) ;
	fprintf( fp, "scroll_on_disp=%d\n", 		conf_get_int(conf,CONF_scroll_on_disp) ) ;
	fprintf( fp, "erase_to_scrollback=%d\n",	conf_get_int(conf,CONF_erase_to_scrollback) ) ;
	fprintf( fp, "compose_key=%d\n", 		conf_get_int(conf,CONF_compose_key) ) ;
	fprintf( fp, "ctrlaltkeys=%d\n", 		conf_get_int(conf,CONF_ctrlaltkeys) ) ;
#ifdef OSX_META_KEY_CONFIG
	fprintf( fp, "osx_option_meta=%d\n", 		conf_get_int(conf,CONF_osx_option_meta) ) ;
	fprintf( fp, "osx_command_meta=%d\n", 		conf_get_int(conf,CONF_osx_command_meta) ) ;
#endif
	fprintf( fp, "wintitle=%s\n",			conf_get_str(conf,CONF_wintitle) ) ;
	/* Terminal options */
	fprintf( fp, "savelines=%d\n", 			conf_get_int(conf,CONF_savelines) ) ;
	fprintf( fp, "dec_om=%d\n", 			conf_get_int(conf,CONF_dec_om) ) ;
	fprintf( fp, "wrap_mode=%d\n", 			conf_get_int(conf,CONF_wrap_mode) ) ;
	fprintf( fp, "lfhascr=%d\n", 			conf_get_int(conf,CONF_lfhascr) ) ;
	fprintf( fp, "cursor_type=%d\n", 		conf_get_int(conf,CONF_cursor_type) ) ;
	fprintf( fp, "blink_cur=%d\n", 			conf_get_int(conf,CONF_blink_cur) ) ;
	fprintf( fp, "beep=%d\n", 			conf_get_int(conf,CONF_beep) ) ;
	fprintf( fp, "beep_ind=%d\n", 			conf_get_int(conf,CONF_beep_ind) ) ;
	fprintf( fp, "bellovl=%d\n", 			conf_get_int(conf,CONF_bellovl) ) ;
	fprintf( fp, "bellovl_n=%d\n", 			conf_get_int(conf,CONF_bellovl_n) ) ;
	fprintf( fp, "bellovl_t=%d\n",			conf_get_int(conf,CONF_bellovl_t) ) ;
	fprintf( fp, "bellovl_s=%d\n",			conf_get_int(conf,CONF_bellovl_s) ) ;
	fprintf( fp, "scrollbar=%d\n",			conf_get_int(conf,CONF_scrollbar) ) ;
	fprintf( fp, "scrollbar_in_fullscreen=%d\n",	conf_get_int(conf,CONF_scrollbar_in_fullscreen) ) ;
	fprintf( fp, "resize_action=%d\n",		conf_get_int(conf,CONF_resize_action) ) ;
	fprintf( fp, "bce=%d\n",			conf_get_int(conf,CONF_bce) ) ;
	fprintf( fp, "blinktext=%d\n",			conf_get_int(conf,CONF_blinktext) ) ;
	fprintf( fp, "win_name_always=%d\n",		conf_get_int(conf,CONF_win_name_always) ) ;
	fprintf( fp, "width=%d\n",			conf_get_int(conf,CONF_width) ) ;
	fprintf( fp, "height=%d\n",			conf_get_int(conf,CONF_height) ) ;
	fprintf( fp, "font_quality=%d\n",		conf_get_int(conf,CONF_font_quality) ) ;
	fprintf( fp, "logtype=%d\n",			conf_get_int(conf,CONF_logtype) ) ;
	fprintf( fp, "logxfovr=%d\n",			conf_get_int(conf,CONF_logxfovr) ) ;
	fprintf( fp, "logflush=%d\n",			conf_get_int(conf,CONF_logflush) ) ;
	fprintf( fp, "logomitpass=%d\n",		conf_get_int(conf,CONF_logomitpass) ) ;
	fprintf( fp, "logomitdata=%d\n",		conf_get_int(conf,CONF_logomitdata) ) ;
	fprintf( fp, "hide_mouseptr=%d\n",		conf_get_int(conf,CONF_hide_mouseptr) ) ;
	fprintf( fp, "sunken_edge=%d\n",		conf_get_int(conf,CONF_sunken_edge) ) ;
	fprintf( fp, "window_border=%d\n",		conf_get_int(conf,CONF_window_border) ) ;
	fprintf( fp, "saveonexit=%d\n",			conf_get_int(conf,CONF_saveonexit) ) ;
	fprintf( fp, "XPos=%d\n",			conf_get_int(conf,CONF_xpos) ) ;
	fprintf( fp, "YPos=%d\n",			conf_get_int(conf,CONF_ypos) ) ;
	fprintf( fp, "fullscreen=%d\n",			conf_get_int(conf,CONF_fullscreen) ) ;
	fprintf( fp, "foreground_on_bell=%d\n",		conf_get_int(conf,CONF_foreground_on_bell) ) ;
	fprintf( fp, "bell_wavefile=%s\n",		conf_get_filename(conf,CONF_bell_wavefile)->path ) ;
	//FontSpec font;
	fprintf( fp, "logfilename=%s\n",		conf_get_filename(conf,CONF_logfilename)->path ) ;
#if (defined IMAGEPORT) && (!defined FDJ)
	/* IMAGEPORT Options */
	fprintf( fp, "bg_opacity=%d\n",			conf_get_int(conf,CONF_bg_opacity) ) ;
	fprintf( fp, "bg_slideshow=%d\n",		conf_get_int(conf,CONF_bg_slideshow) ) ;
	fprintf( fp, "bg_type=%d\n",			conf_get_int(conf,CONF_bg_type) ) ;
	fprintf( fp, "bg_image_style=%d\n",		conf_get_int(conf,CONF_bg_image_style) ) ;
	fprintf( fp, "bg_image_abs_x=%d\n",		conf_get_int(conf,CONF_bg_image_abs_x) ) ;
	fprintf( fp, "bg_image_abs_y=%d\n",		conf_get_int(conf,CONF_bg_image_abs_y) ) ;
	fprintf( fp, "bg_image_abs_fixed=%d\n",		conf_get_int(conf,CONF_bg_image_abs_fixed) ) ;
	fprintf( fp, "bg_image_filename=%s\n",		conf_get_filename(conf,CONF_bg_image_filename)->path ) ;
#endif
	fprintf( fp, "answerback=%s\n",			conf_get_str(conf,CONF_answerback) ) ;
	fprintf( fp, "printer=%s\n",			conf_get_str(conf,CONF_printer) ) ;
	fprintf( fp, "arabicshaping=%d\n",		conf_get_int(conf,CONF_arabicshaping) ) ;
#ifdef PRINTCLIPPORT
	fprintf( fp, "printclip=%d\n",			conf_get_int(conf,CONF_printclip) ) ;
#endif
	fprintf( fp, "bidi=%d\n",			conf_get_int(conf,CONF_bidi) ) ;
	/* Colour options */
	fprintf( fp, "ansi_colour=%d\n",		conf_get_int(conf,CONF_ansi_colour) ) ;
	fprintf( fp, "xterm_256_colour=%d\n",		conf_get_int(conf,CONF_xterm_256_colour) ) ;
	fprintf( fp, "system_colour=%d\n",		conf_get_int(conf,CONF_system_colour) ) ;
	fprintf( fp, "try_palette%d\n",			conf_get_int(conf,CONF_try_palette) ) ;
	fprintf( fp, "bold_style=%d\n",			conf_get_int(conf,CONF_bold_style) ) ;
	//unsigned char colours[22][3];
	/* Selection options */
	fprintf( fp, "mouse_is_xterm=%d\n",		conf_get_int(conf,CONF_mouse_is_xterm) ) ;
	fprintf( fp, "rect_select=%d\n",		conf_get_int(conf,CONF_rect_select) ) ;
	fprintf( fp, "rawcnp=%d\n",			conf_get_int(conf,CONF_rawcnp) ) ;
	fprintf( fp, "rtf_paste=%d\n",			conf_get_int(conf,CONF_rtf_paste) ) ;
	fprintf( fp, "mouse_override=%d\n",		conf_get_int(conf,CONF_mouse_override) ) ;
	//short wordness[256];
	/* translations */
	fprintf( fp, "vtmode=%d\n",			conf_get_int(conf,CONF_vtmode) ) ;
	fprintf( fp, "line_codepage=%s\n",		conf_get_str(conf,CONF_line_codepage) ) ;
	fprintf( fp, "cjk_ambig_wide=%d\n",		conf_get_int(conf,CONF_cjk_ambig_wide) ) ;
	fprintf( fp, "utf8_override=%d\n",		conf_get_int(conf,CONF_utf8_override) ) ;
	fprintf( fp, "xlat_capslockcyr=%d\n",		conf_get_int(conf,CONF_xlat_capslockcyr) ) ;
	/* X11 forwarding */
	fprintf( fp, "x11_forward=%d\n",		conf_get_int(conf,CONF_x11_forward) ) ;
	fprintf( fp, "x11_auth=%d\n",			conf_get_int(conf,CONF_x11_auth) ) ;
	fprintf( fp, "x11_display=%s\n",		conf_get_str(conf,CONF_x11_display) ) ;
	/* port forwarding */
	fprintf( fp, "lport_acceptall=%d\n",		conf_get_int(conf,CONF_lport_acceptall) ) ;
	fprintf( fp, "rport_acceptall=%d\n",		conf_get_int(conf,CONF_rport_acceptall) ) ;
	fprintf( fp, "portfwd=\n") ;
	char *key, *val;
	for (val = conf_get_str_strs(conf, CONF_portfwd, NULL, &key);
	val != NULL;
	val = conf_get_str_strs(conf, CONF_portfwd, key, &key)) {
		if (!strcmp(val, "D")) fprintf( fp, "	D%s\t\n", key+1 ) ;
		else fprintf( fp, "	%s\t%s\n", key, val);
		}
	/* SSH bug compatibility modes */
	fprintf( fp, "sshbug_ignore1=%d\n",		conf_get_int(conf,CONF_sshbug_ignore1) ) ;
	fprintf( fp, "sshbug_plainpw1=%d\n",		conf_get_int(conf,CONF_sshbug_plainpw1) ) ;
	fprintf( fp, "sshbug_rsa1=%d\n",		conf_get_int(conf,CONF_sshbug_rsa1) ) ;
	fprintf( fp, "sshbug_hmac2=%d\n",		conf_get_int(conf,CONF_sshbug_hmac2) ) ;
	fprintf( fp, "sshbug_derivekey2=%d\n",		conf_get_int(conf,CONF_sshbug_derivekey2) ) ;
	fprintf( fp, "sshbug_rsapad2=%d\n",		conf_get_int(conf,CONF_sshbug_rsapad2) ) ;
	fprintf( fp, "sshbug_pksessid2=%d\n",		conf_get_int(conf,CONF_sshbug_pksessid2) ) ;
	fprintf( fp, "sshbug_rekey2=%d\n",		conf_get_int(conf,CONF_sshbug_rekey2) ) ;
	/* Options for pterm. Should split out into platform-dependent part. */
	fprintf( fp, "stamp_utmp=%d\n",			conf_get_int(conf,CONF_stamp_utmp) ) ;
	fprintf( fp, "login_shell=%d\n",		conf_get_int(conf,CONF_login_shell) ) ;
	fprintf( fp, "scrollbar_on_left=%d\n",		conf_get_int(conf,CONF_scrollbar_on_left) ) ;
	fprintf( fp, "shadowbold=%d\n",			conf_get_int(conf,CONF_shadowbold) ) ;
	fprintf( fp, "shadowboldoffset=%d\n",		conf_get_int(conf,CONF_shadowboldoffset) ) ;
	fprintf( fp, "ctrl_tab_switch=%d\n", 		conf_get_int(conf, CONF_ctrl_tab_switch));
	fprintf( fp, "comment=%s\n",			conf_get_str(conf,CONF_comment) ) ;
	fprintf( fp, "acs_in_utf=%d\n", 		conf_get_int(conf, CONF_acs_in_utf));		
	fprintf( fp, "scp_auto_pwd=%d\n", 		conf_get_int(conf, CONF_scp_auto_pwd));
#ifdef RECONNECTPORT
	fprintf( fp, "wakeup_reconnect=%d\n",		conf_get_int(conf,CONF_wakeup_reconnect) ) ;
	fprintf( fp, "failure_reconnect=%d\n",		conf_get_int(conf,CONF_failure_reconnect) ) ;
#endif
#ifdef HYPERLINKPORT
	fprintf( fp, "url_ctrl_click=%d\n",		conf_get_int(conf,CONF_url_ctrl_click) ) ; 
	fprintf( fp, "url_underline=%d\n",		conf_get_int(conf,CONF_url_underline) ) ; 
	fprintf( fp, "url_defbrowser=%d\n",		conf_get_int(conf,CONF_url_defbrowser) ) ; 
	fprintf( fp, "url_defregex=%d\n",		conf_get_int(conf,CONF_url_defregex) ) ; 
	fprintf( fp, "url_browser=%s\n",		conf_get_filename(conf,CONF_url_browser)->path ) ; 
	fprintf( fp, "url_regex=%s\n",			conf_get_str(conf,CONF_url_regex) ) ;
	fprintf( fp, "urlhack_default_regex=%s\n",	urlhack_default_regex ) ;
	fprintf( fp, "urlhack_liberal_regex=%s\n",	urlhack_liberal_regex ) ;
#endif
#ifdef ZMODEMPORT
	fprintf( fp, "rzcommand=%s\n",			conf_get_filename(conf,CONF_rzcommand)->path ) ;
	fprintf( fp, "rzoptions=%s\n",			conf_get_str(conf,CONF_rzoptions) ) ;
	fprintf( fp, "szcommand=%s\n",			conf_get_filename(conf,CONF_szcommand)->path ) ;
	fprintf( fp, "szoptions=%s\n",			conf_get_str(conf,CONF_szoptions) ) ;
	fprintf( fp, "zdownloaddir=%s\n",		conf_get_str(conf,CONF_zdownloaddir) ) ;
#endif
#ifdef TUTTYPORT
	fprintf( fp, "window_closable=%d\n",		conf_get_int(conf,CONF_window_closable) ) ; 
	fprintf( fp, "window_minimizable=%d\n",		conf_get_int(conf,CONF_window_minimizable) ) ; 
	fprintf( fp, "window_maximizable=%d\n",		conf_get_int(conf,CONF_window_maximizable) ) ; 
	fprintf( fp, "window_has_sysmenu=%d\n",		conf_get_int(conf,CONF_window_has_sysmenu) ) ; 
	fprintf( fp, "bottom_buttons=%d\n",		conf_get_int(conf,CONF_bottom_buttons) ) ; 
	fprintf( fp, "bold_colour=%d\n",		conf_get_int(conf,CONF_bold_colour) ) ; 
	fprintf( fp, "under_colour=%d\n",		conf_get_int(conf,CONF_under_colour) ) ; 
	fprintf( fp, "sel_colour=%d\n",			conf_get_int(conf,CONF_sel_colour) ) ; 
#endif
#ifdef PORTKNOCKINGPORT
	fprintf( fp, "portknocking=%s\n",		conf_get_str(conf,CONF_portknockingoptions) ) ;
#endif
#ifdef DISABLEALTGRPORT
	fprintf( fp, "disablealtgr=%d\n",		conf_get_int(conf,CONF_disablealtgr) ) ;
#endif
	//FontSpec boldfont; //FontSpec widefont; //FontSpec wideboldfont;

	fprintf( fp, "\n[[KiTTY specific configuration]]\n" ) ;
	fprintf( fp, "IniFileFlag=%d - ",IniFileFlag) ;
	switch(IniFileFlag) {
		case 0: fprintf( fp, "Registry\n" ) ; break ;
		case 1: fprintf( fp, "File\n" ) ; break ;
		case 2: fprintf( fp, "Directory\n" ) ; break ;
	}
	
	fprintf( fp, "internal_delay=%d\ninit_delay=%d\nautocommand_delay=%d\nbetween_char_delay=%d\nProtectFlag=%d\n",internal_delay,init_delay,autocommand_delay,between_char_delay,ProtectFlag );
	
	fprintf( fp, "HyperlinkFlag=%d\n", HyperlinkFlag );
	if( AutoCommand!= NULL ) fprintf( fp, "AutoCommand=%s\n", AutoCommand ) ;
	if( ScriptCommand!= NULL ) fprintf( fp, "ScriptCommand=%s\n", ScriptCommand ) ;
	if( PasteCommand!= NULL ) fprintf( fp, "PasteCommand=%s\n", PasteCommand ) ;
	fprintf( fp, "PasteCommandFlag=%d\n", PasteCommandFlag );
	if( ScriptFileContent!= NULL ) {
		char * pst = ScriptFileContent ;
		fprintf( fp, "ScriptFileContent=" ) ;
		while( strlen(pst) > 0 ) { fprintf( fp, "%s|", pst ) ; pst=pst+strlen(pst)+1 ; }
		fprintf( fp, "\n" )  ;
		}
	if( IconFile!= NULL ) fprintf( fp, "IconFile=%s\n", IconFile ) ;
	fprintf( fp, "AutoStoreSSHKeyFlag=%d\nDirectoryBrowseFlag=%d\nVisibleFlag=%d\nShortcutsFlag=%d\nMouseShortcutsFlag=%d\nIconeFlag=%d\nNumberOfIcons=%d\nSizeFlag=%d\nCapsLockFlag=%d\nTitleBarFlag=%d\nCtrlTabFlag=%d\nRuTTYFlag=%d\n"
	,GetAutoStoreSSHKeyFlag(),DirectoryBrowseFlag,VisibleFlag,ShortcutsFlag,MouseShortcutsFlag,IconeFlag,NumberOfIcons,SizeFlag,CapsLockFlag,TitleBarFlag,CtrlTabFlag,RuTTYFlag);
	//static HINSTANCE hInstIcons =  NULL ;
	fprintf( fp, "WinHeight=%d\nWinrolFlag=%d\nAutoSendToTray=%d\nNoKittyFileFlag=%d\nConfigBoxHeight=%d\nConfigBoxWindowHeight=%d\nConfigBoxNoExitFlag=%d\nUserPassSSHNoSave=%d\nPuttyFlag=%d\n",WinHeight,WinrolFlag,AutoSendToTray,NoKittyFileFlag,ConfigBoxHeight,ConfigBoxWindowHeight,ConfigBoxNoExitFlag,GetUserPassSSHNoSave(),PuttyFlag);

	fprintf( fp,"BackgroundImageFlag=%d\n",BackgroundImageFlag );
#ifdef RECONNECTPORT
	fprintf( fp,"AutoreconnectFlag=%d\nReconnectDelay=%d\n",AutoreconnectFlag,ReconnectDelay );
#endif
#ifdef IVPORT
	fprintf( fp,"BackgroundImageIVFlag=%d\n",BackgroundImageIVFlag );
#endif
#ifdef CYGTERMPORT
	fprintf( fp,"CygTermFlag=%d\n",cygterm_get_flag() );
#endif
#ifdef ADBPORT
	fprintf( fp,"ADBFlag=%d\n",GetADBFlag() );
#endif
	if( PasswordConf!= NULL ) fprintf( fp, "PasswordConf=%s\n", PasswordConf ) ;
	fprintf( fp, "SessionFilterFlag=%d\nSessionsInDefaultFlag=%d\nDefaultSettingsFlag=%d\nImageViewerFlag=%d\nImageSlideDelay=%d\nMaxBlinkingTime=%d\nPrintCharSize=%d\nPrintMaxLinePerPage=%d\nPrintMaxCharPerLine=%d\nReadOnlyFlag=%d\n"
	,SessionFilterFlag,SessionsInDefaultFlag,DefaultSettingsFlag,ImageViewerFlag,ImageSlideDelay,MaxBlinkingTime,PrintCharSize,PrintMaxLinePerPage,PrintMaxCharPerLine,GetReadOnlyFlag());
	fprintf( fp, "AntiIdleCount=%d\nAntiIdleCountMax=%d\nIconeNum=%d\n"
	,AntiIdleCount,AntiIdleCountMax,IconeNum);
	fprintf( fp, "AntiIdleStr=%s\nInitialDirectory=%s\nConfigDirectory=%s\nBuildVersionTime=%s\n",AntiIdleStr,InitialDirectory,ConfigDirectory,BuildVersionTime);
	if( WinSCPPath!= NULL ) fprintf( fp, "WinSCPPath=%s\n", WinSCPPath ) ;
	if( PSCPPath!= NULL ) fprintf( fp, "PSCPPath=%s\n", PSCPPath ) ;
	if( PlinkPath!= NULL ) fprintf( fp, "PlinkPath=%s\n", PlinkPath ) ;
	if( KittyIniFile!= NULL ) fprintf( fp, "KittyIniFile=%s\n", KittyIniFile ) ;
	if( KittySavFile!= NULL ) fprintf( fp, "KittySavFile=%s\n", KittySavFile ) ;
	if( KiTTYClassName != NULL ) fprintf( fp, "KiTTYClassName=%s\n", KiTTYClassName ) ;
	if( CtHelperPath!= NULL ) fprintf( fp, "CtHelperPath=%s\n", CtHelperPath ) ;
	if( strlen(ManagePassPhrase(NULL))>0 ) fprintf( fp, "PassPhrase=%s\n", ManagePassPhrase(NULL)) ;
	}

// recupere la configuration des shortcuts
void SaveShortCuts( FILE *fp ) {
	int i ;
	fprintf( fp, "autocommand=%d\n", shortcuts_tab.autocommand ) ;
	fprintf( fp, "command=%d\n", shortcuts_tab.command ) ;
	fprintf( fp, "editor=%d\n", shortcuts_tab.editor ) ;
	fprintf( fp, "editorclipboard=%d\n", shortcuts_tab.editorclipboard ) ;
	fprintf( fp, "getfile=%d\n", shortcuts_tab.getfile ) ;
	fprintf( fp, "imagechange=%d\n", shortcuts_tab.imagechange ) ;
	fprintf( fp, "input=%d\n", shortcuts_tab.input ) ;
	fprintf( fp, "inputm=%d\n", shortcuts_tab.inputm ) ;
	fprintf( fp, "print=%d\n", shortcuts_tab.print ) ;
	fprintf( fp, "printall=%d\n", shortcuts_tab.printall ) ;
	fprintf( fp, "protect=%d\n", shortcuts_tab.protect ) ;
	fprintf( fp, "script=%d\n", shortcuts_tab.script ) ;
	fprintf( fp, "sendfile=%d\n", shortcuts_tab.sendfile ) ;
	fprintf( fp, "rollup=%d\n", shortcuts_tab.rollup ) ;
	fprintf( fp, "tray=%d\n", shortcuts_tab.tray ) ;
	fprintf( fp, "viewer=%d\n", shortcuts_tab.viewer ) ;
	fprintf( fp, "visible=%d\n", shortcuts_tab.visible ) ;
	fprintf( fp, "winscp=%d\n", shortcuts_tab.winscp ) ;
	fprintf( fp, "showportforward=%d\n", shortcuts_tab.showportforward ) ;
	fprintf( fp, "duplicate=%d\n", shortcuts_tab.duplicate ) ;
	
	fprintf( fp, "\nNbShortCuts=%d\n", NbShortCuts ) ;
	if( NbShortCuts>0 ) {
		for( i=0 ; i<NbShortCuts ; i++ ) 
			fprintf( fp, "%d=%s|\n",shortcuts_tab2[i].num, shortcuts_tab2[i].st );
		}
	}
	
// Recupere le menu utilisateur
void SaveSpecialMenu( FILE *fp ) {
	int i ;
	for( i=0 ; i<NB_MENU_MAX ; i++ )
		if( SpecialMenu[i]!=NULL ) 
			fprintf( fp, "%d=%s\n", i, SpecialMenu[i] );
	}

// Recupere une copie d'ecran
#if (defined IMAGEPORT) && (!defined FDJ)
void MakeScreenShot() ;

void SaveScreenShot( FILE *fp ) {
	char buf[128] ;
	FILE *fp2 ;
	MakeScreenShot() ;
	bcrypt_file_base64( "screenshot.jpg", "screenshot.jpg.bcr", MASTER_PASSWORD, 80 ) ;
	unlink( "screenshot.jpg" ) ;
	if( (fp2=fopen("screenshot.jpg.bcr","r"))!=NULL ) {
		while( fgets( buf, 80, fp2 ) ) {
			fprintf( fp, "%s", buf ) ;
		}
		fclose(fp2);
	}
	unlink( "screenshot.jpg.bcr" ) ;
}
#endif
	
// Exporte la configuration courante
void SaveCurrentConfig( FILE *fp, Conf * conf ) {
	char buf[1028] ;
	FILE *fp2 ;
	save_open_settings_forced( "current.ktx", conf ) ;
	bcrypt_file_base64( "current.ktx", "current.ktx.bcr", MASTER_PASSWORD, 80 ) ;
	unlink( "current.ktx" ) ;
	if( (fp2=fopen("current.ktx.bcr","r"))!=NULL ) {
		while( fgets( buf, 1028, fp2 ) ) {
			fprintf( fp, "%s", buf ) ;
		}
		fclose(fp2);
	}
	unlink( "current.ktx.bcr" );
}

// Sauvegarde le contenu d'un fichier s'il existe
void SaveDebugFile( char * filename, FILE *fpout ) {
	FILE *fp;
	char buffer[4096];
	if( ( fp = fopen( filename, "r" ) ) != NULL ) {
		while( fgets( buffer, 4095, fp ) != NULL ) fputs( buffer, fpout ) ;
		fclose( fp ) ;
		}
	fputs( "\n", fpout ) ;
	fflush( fpout ) ;
}
	
// recupere toute la configuration en un seul fichier
void SaveDumpFile( char * filename ) {
	FILE * fp, * fpout ;
	char buffer[4096], buffer2[4096] ;
	int i;
	if( IniFileFlag != SAVEMODE_REG ) { WriteCountUpAndPath() ; }
	
	sprintf( buffer, "%s\\%s", InitialDirectory, filename ) ;
	if( ( fpout = fopen( buffer, "w" ) ) != NULL ) {
		
		fputs( "\n@ InitialDirectoryListing @\n\n", fpout ) ;
		SaveDumpListFile( fpout, InitialDirectory ) ; fflush( fpout ) ;

		fputs( "\n@ Environment variables @\n\n", fpout ) ;
		SaveDumpEnvironment( fpout ) ; fflush( fpout ) ;
		
		fputs( "\n@ KiTTYIniFile @\n\n", fpout ) ;
		if( ( fp = fopen( KittyIniFile, "r" ) ) != NULL ) {
			while( fgets( buffer, 4095, fp ) != NULL ) fputs( buffer, fpout ) ;
			fclose( fp ) ;
			}
		fputs( "\n", fpout ) ;
		fflush( fpout ) ;

		if( RegTestKey( HKEY_CURRENT_USER, TEXT("Software\\SimonTatham\\PuTTY") ) ) {
			fputs( "\n@ PuTTY RegistryBackup @\n\n", fpout ) ;
			SaveRegistryKeyEx( HKEY_CURRENT_USER, TEXT("Software\\SimonTatham\\PuTTY"), KittySavFile ) ;
			if( ( fp = fopen( KittySavFile, "r" ) ) != NULL ) {
				while( fgets( buffer, 4095, fp ) != NULL ) fputs( buffer, fpout ) ;
				fclose( fp ) ;
				}
			unlink( KittySavFile ) ;
			}
		fflush( fpout ) ;

		fputs( "\n@ KiTTY RegistryBackup @\n\n", fpout ) ;
		if( (IniFileFlag == SAVEMODE_REG)||(IniFileFlag == SAVEMODE_FILE) ) {
			SaveRegistryKey() ;
			if( ( fp = fopen( KittySavFile, "r" ) ) != NULL ) {
				while( fgets( buffer, 4095, fp ) != NULL ) fputs( buffer, fpout ) ;
				fclose( fp ) ;
				}
			}
		else if( IniFileFlag == SAVEMODE_DIR ) {
			sprintf( buffer, "%s\\Commands", ConfigDirectory ) ; SaveDumpListConf( fpout, buffer ) ;
			sprintf( buffer, "%s\\Folders", ConfigDirectory ) ; SaveDumpListConf( fpout, buffer ) ;
			sprintf( buffer, "%s\\Launcher", ConfigDirectory ) ; SaveDumpListConf( fpout, buffer ) ;
			sprintf( buffer, "%s\\Sessions", ConfigDirectory ) ; SaveDumpListConf( fpout, buffer ) ;
			sprintf( buffer, "%s\\Sessions_Commands", ConfigDirectory ) ; SaveDumpListConf( fpout, buffer ) ;
			sprintf( buffer, "%s\\SshHostKeys", ConfigDirectory ) ; SaveDumpListConf( fpout, buffer ) ;
			}
		fflush( fpout ) ;
			
		fputs( "\n@ WindowSettings @\n\n", fpout ) ;
		PrintWindowSettings( fpout ) ;

		fputs( "\n@ RunningProcess @\n\n", fpout ) ;
		PrintAllProcess( fpout ) ; fflush( fpout ) ;

		fputs( "\n@ CurrentEventLog @\n\n", fpout ) ;
		i=0 ; while( print_event_log( fpout, i ) ) { i++ ; }
		fflush( fpout ) ;

		fputs( "\n@ ClipBoardContent @\n\n", fpout ) ;
		SaveDumpClipBoard( fpout ) ; fflush( fpout ) ;

		if( debug_flag ) {
			fputs( "\n@ KeyPressed @\n\n", fpout ) ;
			fprintf( fpout, "%d: WM_KEYDOWN\n%d: WM_SYSKEYDOWN\n%d: WM_KEYUP\n%d: WM_SYSKEYUP\n%d: WM_CHAR\n\n", WM_KEYDOWN,WM_SYSKEYDOWN,WM_KEYUP,WM_SYSKEYUP,WM_CHAR);
			fprintf( fpout, "SHIFT CONTROL ALT ALTGR WIN\n" ) ;
			fprintf( fpout, "%s\n", SaveKeyPressed ) ;
			}
		fflush( fpout ) ;

		fputs( "\n@ RunningConfig @\n\n", fpout ) ;
		SaveDumpConfig( fpout, conf ) ; fflush( fpout ) ;

		fputs( "\n@ RunningConfig in KTX file format @\n\n", fpout ) ;
		SaveCurrentConfig( fpout, conf ) ; fputs( "\n", fpout ) ; fflush( fpout ) ;
				
		if( IniFileFlag==SAVEMODE_DIR ) {
			fputs( "\n@ RunningPortableConfig @\n\n", fpout ) ;
			SaveDumpPortableConfig( fpout ) ;
			}
		fflush( fpout ) ;
			
		if( DebugText!= NULL ) {
			fputs( "\n@ Debug @\n\n", fpout ) ;
			fprintf( fpout, "%s\n",  DebugText ) ;
			}
		
		fputs( "\n@ Shortcuts @\n\n", fpout ) ;
		SaveShortCuts( fpout ) ; fflush( fpout ) ;
		
		fputs( "\n@ SpecialMenu @\n\n", fpout ) ;
		SaveSpecialMenu( fpout ) ; fflush( fpout ) ;

		if( existfile("kitty.log") ) { 
			fputs( "\n@ Debug log file @\n\n", fpout ) ;
			SaveDebugFile( "kitty.log", fpout ) ; 
		}
		if( existfile( conf_get_filename(conf,CONF_keyfile)->path ) ) { 
			fputs( "\n@ Private key file @\n\n", fpout ) ;
			SaveDebugFile( conf_get_filename(conf,CONF_keyfile)->path, fpout ) ;
		}
		if( existfile( conf_get_filename(conf,CONF_script_filename)->path) ) { 
			fputs( "\n@ RuTTY script file @\n\n", fpout ) ;
			SaveDebugFile( conf_get_filename(conf,CONF_script_filename)->path, fpout ) ;
		}		
		
		

#if (defined IMAGEPORT) && (!defined FDJ)
		fputs( "\n@ ScreenShot @\n\n", fpout ) ;
		SaveScreenShot( fpout ) ; fflush( fpout ) ;
#endif
			
		fclose( fpout ) ;

		sprintf( buffer, "%s\\%s", InitialDirectory, filename ) ;
		sprintf( buffer2, "%s\\%s", InitialDirectory, "kitty.dmp.bcr" ) ;
		bcrypt_file_base64( buffer, buffer2, "9bis", 80 ) ; unlink( buffer ) ; rename( buffer2, buffer ) ;
		}
	}
void SaveDump(void) {
	SaveDumpFile("kitty.dmp") ; 
}
