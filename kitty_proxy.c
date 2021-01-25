#include "putty.h"
#include "kitty.h"
#include "kitty_tools.h"
#include "kitty_registry.h"
#include "kitty_proxy.h"
#include "kitty_store.h"
#include "dialog.h"
#include "storage.h"
#include <sys/types.h>
#include <dirent.h>
#include <windows.h>


// Flag pour ajouter la fonction Proxy Selector
static int ProxySelectionFlag = 0 ;
int GetProxySelectionFlag() { return ProxySelectionFlag ; }
void SetProxySelectionFlag( const int flag ) { ProxySelectionFlag = flag ; }

void debug_logevent( const char *fmt, ... ) ;

struct Proxies proxies[MAX_PROXY] ;
    
void InitProxyList(void) {
	HKEY hKey ;
	int i,j;
	char buffer[MAX_VALUE_NAME] ;
	for( i=0; i<lenof(proxies); i++) {
		proxies[i].name = NULL;
		proxies[i].val = i;
	}
	proxies[0].name=(char*)malloc(26); strcpy(proxies[0].name,"- Session defined proxy -");
	proxies[1].name=(char*)malloc(13); strcpy(proxies[1].name,"- No proxy -");
	j=2;
	if( (IniFileFlag == SAVEMODE_REG)||(IniFileFlag == SAVEMODE_FILE) ) {
		TCHAR 	achClass[MAX_PATH] = TEXT("");
		DWORD   cchClassName=MAX_PATH,cSubKeys=0,cbMaxSubKey,cchMaxClass;
		DWORD	cValues,cchMaxValue,cbMaxValueData,cbSecurityDescriptor;
		FILETIME ftLastWriteTime;
		sprintf( buffer, "%s\\Proxies", PUTTY_REG_POS ) ;
		RegTestOrCreate( HKEY_CURRENT_USER, buffer, NULL, NULL ) ;
		if( RegOpenKeyEx( HKEY_CURRENT_USER, buffer, 0, KEY_READ, &hKey) != ERROR_SUCCESS ) return ;
		RegQueryInfoKey(hKey,achClass,&cchClassName,NULL,&cSubKeys,&cbMaxSubKey,&cchMaxClass,&cValues,&cchMaxValue,&cbMaxValueData,&cbSecurityDescriptor,&ftLastWriteTime);
		if( cSubKeys>0 )
			for (i=0; i<cSubKeys; i++) {
				DWORD cchValue = MAX_VALUE_NAME; 
				char lpData[4096] ;
				if( RegEnumKeyEx(hKey, i, lpData, &cchValue, NULL, NULL, NULL, &ftLastWriteTime) == ERROR_SUCCESS ) {
					if( strcmp(lpData,"None") && strcmp(lpData,"Default") ) {
						proxies[j].name=(char*)malloc(strlen(lpData)+1);
						unmungestr( lpData, proxies[j].name, MAX_VALUE_NAME ) ;
						j++;
					}
				}
			}
		RegCloseKey( hKey ) ;
	} else if( IniFileFlag == SAVEMODE_DIR ) {
		char fullpath[MAX_VALUE_NAME];
		DIR * dir ;
		struct dirent * de ;
		sprintf( fullpath, "%s\\Proxies", ConfigDirectory ) ;
		if(!MakeDir( fullpath ) ) { MessageBox(NULL,"Unable to create the proxy definitions directory","Error",MB_OK|MB_ICONERROR); }
		if( (dir=opendir(fullpath)) != NULL ) {
			while( (de=readdir(dir)) != NULL )
			if( strcmp(de->d_name,".") && strcmp(de->d_name,"..") )	{
				sprintf( fullpath, "%s\\Proxies\\%s", ConfigDirectory, de->d_name ) ;
				if( !(GetFileAttributes( fullpath ) & FILE_ATTRIBUTE_NORMAL) ) {
					if( strcmp(de->d_name,"None") && strcmp(de->d_name,"Default") ) {
						proxies[j].name=(char*)malloc(strlen(de->d_name)+1);
						unmungestr( de->d_name, proxies[j].name, MAX_VALUE_NAME ) ;
						j++;
					}
				}
			}
			closedir(dir) ;
		}
	}
}

int LoadProxyInfo( Conf * conf, const char * name ) {
	char buffer[MAX_VALUE_NAME] ;
	if( !strcmp(name,"- Session defined proxy -") ) { return 0 ; }
	if( !strcmp(name,"- No proxy -") ) { 
		debug_logevent( "Remove proxy definition" ) ;
		conf_set_int(conf, CONF_proxy_type, PROXY_NONE) ; 
		return 1 ;
	}
	debug_logevent( "Load proxy \"%s\" definition", name ) ;
	if( (IniFileFlag == SAVEMODE_REG)||(IniFileFlag == SAVEMODE_FILE) ) {
		HKEY hKey ;
		sprintf( buffer, "%s\\Proxies\\", PUTTY_REG_POS ) ;
		char *b = (char*)malloc(4*strlen(name)+1);
		mungestr(name,b);
		strcat(buffer,b);
		free(b);
		if( RegOpenKeyEx( HKEY_CURRENT_USER, buffer, 0, KEY_READ, &hKey) != ERROR_SUCCESS ) {
			debug_logevent( "Unable to load proxy definition" ) ;
			return 0;
		}
		char lpData[4096] ;
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyExcludeList", lpData ) ) { 
			conf_set_str( conf, CONF_proxy_exclude_list, lpData ) ; 
		}
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyDNS", lpData ) ) {
			int i=atoi(lpData);
			conf_set_int(conf, CONF_proxy_dns, (i+1)%3);
		}
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyLocalhost", lpData ) ) { 
			if( atoi(lpData) == 0 ) { conf_set_bool( conf, CONF_even_proxy_localhost, false ) ; 
			} else { conf_set_bool( conf, CONF_even_proxy_localhost, true ) ;
			}			
		}
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyMethod", lpData ) ) {
			int i = atoi(lpData) ;
			if (i == 0) conf_set_int(conf, CONF_proxy_type, PROXY_NONE);
			else if (i == 1) conf_set_int(conf, CONF_proxy_type, PROXY_SOCKS4) ;
			else if (i == 2) conf_set_int(conf, CONF_proxy_type, PROXY_SOCKS5) ;
			else if (i == 3) conf_set_int(conf, CONF_proxy_type, PROXY_HTTP) ;
			else if (i == 4) conf_set_int(conf, CONF_proxy_type, PROXY_TELNET) ;
			else if (i == 5) conf_set_int(conf, CONF_proxy_type, PROXY_CMD) ;
			else conf_set_int(conf, CONF_proxy_type, PROXY_NONE) ; 
		}
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyHost", lpData ) ) { conf_set_str( conf, CONF_proxy_host, lpData ) ; }
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyPort", lpData ) ) { conf_set_int( conf, CONF_proxy_port, atoi(lpData) ) ; }
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyUsername", lpData ) ) { conf_set_str( conf, CONF_proxy_username, lpData ) ; }
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyPassword", lpData ) ) { conf_set_str( conf, CONF_proxy_password, lpData ) ; }
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyTelnetCommand", lpData ) ) { conf_set_str( conf, CONF_proxy_telnet_command, lpData ) ; }
		if( GetValueData(HKEY_CURRENT_USER, buffer, "ProxyLogToTerm", lpData ) ) { conf_set_int( conf, CONF_proxy_log_to_term, atoi(lpData) ) ; }
		RegCloseKey( hKey ) ;
	} else if( IniFileFlag == SAVEMODE_DIR ) {
		char fullpath[MAX_VALUE_NAME] ;
		char *filename = (char*) malloc( 4*strlen(name)+1 ) ;
		mungestr( name, filename ) ;
		sprintf( fullpath, "%s\\Proxies\\%s", ConfigDirectory, filename ) ;
		if( existfile(fullpath) ) {
			FILE *fp;
			if( (fp=fopen(fullpath,"r")) != NULL ) {
				char buf2[MAX_VALUE_NAME]="";
				while( fgets(buffer,MAX_VALUE_NAME,fp)!=NULL ) {
					if( ReadPortableValue(buffer, "ProxyExcludeList", buf2, MAX_VALUE_NAME) ) {
						conf_set_str( conf, CONF_proxy_exclude_list, buf2 ) ; 
					} else if( ReadPortableValue(buffer, "ProxyDNS", buf2, MAX_VALUE_NAME) ) {
						int i=atoi(buf2);
						conf_set_int(conf, CONF_proxy_dns, (i+1)%3);
					} else if( ReadPortableValue(buffer, "ProxyLocalhost", buf2, MAX_VALUE_NAME) ) {
						if( atoi(buf2) == 0 ) { conf_set_bool( conf, CONF_even_proxy_localhost, false ) ; 
						} else { conf_set_bool( conf, CONF_even_proxy_localhost, true ) ;
						}	
					} else if( ReadPortableValue(buffer, "ProxyMethod", buf2, MAX_VALUE_NAME) ) {
						int i = atoi(buf2) ;
						if (i == 0) conf_set_int(conf, CONF_proxy_type, PROXY_NONE);
						else if (i == 1) conf_set_int(conf, CONF_proxy_type, PROXY_SOCKS4) ;
						else if (i == 2) conf_set_int(conf, CONF_proxy_type, PROXY_SOCKS5) ;
						else if (i == 3) conf_set_int(conf, CONF_proxy_type, PROXY_HTTP) ;
						else if (i == 4) conf_set_int(conf, CONF_proxy_type, PROXY_TELNET) ;
						else if (i == 5) conf_set_int(conf, CONF_proxy_type, PROXY_CMD) ;
						else conf_set_int(conf, CONF_proxy_type, PROXY_NONE) ; 
					} else if( ReadPortableValue(buffer, "ProxyHost", buf2, MAX_VALUE_NAME) ) { 
						conf_set_str( conf, CONF_proxy_host, buf2 ) ; 
					} else if( ReadPortableValue(buffer, "ProxyPort", buf2, MAX_VALUE_NAME) ) { 
						conf_set_int( conf, CONF_proxy_port, atoi(buf2) ) ; 
					} else if( ReadPortableValue(buffer, "ProxyUsername", buf2, MAX_VALUE_NAME) ) { 
						conf_set_str( conf, CONF_proxy_username, buf2 ) ; 
					} else if( ReadPortableValue(buffer, "ProxyPassword", buf2, MAX_VALUE_NAME) ) { 
						conf_set_str( conf, CONF_proxy_password, buf2 ) ; 
					} else if( ReadPortableValue(buffer, "ProxyTelnetCommand", buf2, MAX_VALUE_NAME) ) {
						conf_set_str( conf, CONF_proxy_telnet_command, buf2 ) ; 
					} else if( ReadPortableValue(buffer, "ProxyLogToTerm", buf2, MAX_VALUE_NAME) ) { 
						conf_set_int( conf, CONF_proxy_log_to_term, atoi(buf2) ) ; 
					}
				}
				fclose(fp);
			}
		}
		free( filename ) ;
	}
	return 1;
}
