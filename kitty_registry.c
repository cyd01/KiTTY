#include "kitty_registry.h"

//static const int cstMaxRegLength = 1024;
#define cstMaxRegLength 1024

char * itoa (int __val, char *__s, int __radix) ;
char * GetValueData(HKEY hkTopKey, char * lpSubKey, const char * lpValueName, char * rValue){
    HKEY hkKey;
    DWORD lpType, dwDataSize = cstMaxRegLength;
  
  //Receptionne la valeur de réception lecture clé registre
    //unsigned char * lpData = new unsigned char[cstMaxRegLength];
	unsigned char * lpData = (unsigned char*) malloc( cstMaxRegLength );
    
  //Receptionne la valeur de réception lecture clé registre
    //char * rValue = (char*) malloc( cstMaxRegLength );
    rValue[0] = '\0';
  //Lecture de la clé registre si ok passe à la suite...
    if (RegOpenKeyEx(hkTopKey,lpSubKey,0,KEY_READ,&hkKey) == ERROR_SUCCESS){
  
      if (RegQueryValueEx(hkKey,lpValueName,NULL,&lpType,lpData,&dwDataSize) == ERROR_SUCCESS){
      //déchiffrage des différents type de clé dans registry
        switch ((int)lpType){
  
          case REG_BINARY:
               itoa((u_int)(lpData[0]),rValue, 10);
               strcat(rValue,".");
               itoa((u_int)(lpData[1]),(char*)(rValue+strlen(rValue)),10);
               strcat(rValue,".");
               itoa((u_int)(lpData[2]),(char*)(rValue+strlen(rValue)),10);
               strcat(rValue,".");
               itoa((u_int)(lpData[3]),(char*)(rValue+strlen(rValue)),10);
               break;
  
          case REG_DWORD:
               itoa(*(int*)(lpData),rValue,10);
               break;
  
          case REG_EXPAND_SZ:
               //rValue=(char *)lpData;
               strcpy( rValue, (char*)lpData ) ;
               break;
  
          case REG_MULTI_SZ:
               //rValue=(char *)lpData;
               strcpy( rValue, (char*)lpData ) ;
               break;
  
          case REG_SZ:
               //rValue=(char *)lpData;
               strcpy( rValue, (char*)lpData ) ;
               break;
        }//end switch
      }//end if
      else { RegCloseKey(hkKey); free(lpData); return NULL ; }
       free(lpData); // libère la mémoire
       RegCloseKey(hkKey); 
      
    }//end if
    else { return NULL ; }
    return rValue;
  }//end function

// Teste l'existance d'une clé
int RegTestKey( HKEY hMainKey, LPCTSTR lpSubKey ) {
	HKEY hKey ;
	if( lpSubKey == NULL ) return 1 ;
	if( strlen( lpSubKey ) == 0 ) return 1 ;
	if( RegOpenKeyEx( hMainKey, TEXT(lpSubKey), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS ) return 0 ;
	RegCloseKey( hKey ) ;
	return 1 ;
	}
	
// Retourne le nombre de sous-keys
int RegCountKey( HKEY hMainKey, LPCTSTR lpSubKey ) {
	HKEY hKey ;
	TCHAR    achClass[MAX_PATH] = TEXT("");
	DWORD    cchClassName = MAX_PATH, cSubKeys=0, cbMaxSubKey, cchMaxClass, cValues, cchMaxValue, cbMaxValueData, cbSecurityDescriptor ;
	FILETIME ftLastWriteTime;

	int nb = 0 ;
	if( RegOpenKeyEx( hMainKey, TEXT(lpSubKey), 0, KEY_READ, &hKey) != ERROR_SUCCESS ) return 0 ;
	
	RegQueryInfoKey( hKey, achClass, &cchClassName, NULL, &cSubKeys, &cbMaxSubKey, &cchMaxClass
		, &cValues, &cchMaxValue, &cbMaxValueData, &cbSecurityDescriptor, &ftLastWriteTime) ;
	nb = cSubKeys ;
	RegCloseKey( hKey ) ;
	return nb ;
	}

	// Teste l'existance d'une clé ou bien d'une valeur et la crée sinon
void RegTestOrCreate( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR name, LPCTSTR value ) {
	HKEY hKey ;
	if( lpSubKey == NULL ) return ;
	if( strlen( lpSubKey ) == 0 ) return ;
	if( RegOpenKeyEx( hMainKey, TEXT(lpSubKey), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS ) {
		RegCreateKey( hMainKey, lpSubKey, &hKey ) ;
		}
	if( name != NULL ) {
		RegSetValueEx( hKey, TEXT( name ), 0, REG_SZ, (const BYTE *)value, strlen(value)+1 ) ;
		}
	RegCloseKey( hKey ) ;
	}
	
// Test l'existance d'une clé ou bien d'une valeur DWORD et la crée sinon
void RegTestOrCreateDWORD( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR name, DWORD value ) {
	HKEY hKey ;
	if( lpSubKey == NULL ) return ;
	if( strlen( lpSubKey ) == 0 ) return ;
	if( RegOpenKeyEx( hMainKey, TEXT(lpSubKey), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS ) {
		RegCreateKey( hMainKey, lpSubKey, &hKey ) ;
		}
	if( name != NULL ) {
		RegSetValueEx( hKey, TEXT( name ), 0, REG_DWORD, (LPBYTE)&value, sizeof(DWORD) ) ;
		}
	RegCloseKey( hKey ) ;
	}
	

// Initialise toutes les sessions avec une valeur (si oldvalue==NULL) ou uniquement celles qui ont la valeur oldvalue
void RegUpdateAllSessions( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR name, LPCTSTR oldvalue, LPCTSTR value  ) {
	HKEY hKey ;
	TCHAR    achClass[MAX_PATH] = TEXT(""), achKey[MAX_KEY_LENGTH]; 
	DWORD    cchClassName = MAX_PATH, cSubKeys=0, cbMaxSubKey, cbName=MAX_KEY_LENGTH, cchMaxClass, cValues, cchMaxValue, cbMaxValueData, cbSecurityDescriptor ;
	FILETIME ftLastWriteTime;
	
	int i, retCode ;
	if( RegOpenKeyEx( hMainKey, TEXT(lpSubKey), 0, KEY_READ, &hKey) != ERROR_SUCCESS ) return ;
	RegQueryInfoKey( hKey, achClass, &cchClassName, NULL, &cSubKeys, &cbMaxSubKey, &cchMaxClass
		, &cValues, &cchMaxValue, &cbMaxValueData, &cbSecurityDescriptor, &ftLastWriteTime) ;

	if (cSubKeys) {
		for (i=0; i<cSubKeys; i++) {
			retCode = RegEnumKeyEx(hKey, i, achKey, &cbName, NULL, NULL, NULL, &ftLastWriteTime); 
			if (retCode == ERROR_SUCCESS) {
				char buffer[MAX_KEY_LENGTH] ;
				char previousvalue[1024] ;
				sprintf( buffer, "%s\\%s", lpSubKey, achKey ) ;
				GetValueData( hMainKey, buffer, name, previousvalue ) ;
				if( (oldvalue==NULL) || ( !strcmp(previousvalue,oldvalue)) )
					MessageBox(NULL,achKey,"Info",MB_OK);
					//RegTestOrCreate( hMainKey, buffer, name, value ) ;
			}
		}
	}
}
	
// Exporte toute une cle de registre
void QuerySubKey( HKEY hMainKey, LPCTSTR lpSubKey, FILE * fp_out, char * text  ) { 
	HKEY hKey ;
    TCHAR    achKey[MAX_KEY_LENGTH];   // buffer for subkey name
    DWORD    cbName;                   // size of name string 
    TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
    DWORD    cchClassName = MAX_PATH;  // size of class string 
    DWORD    cSubKeys=0;               // number of subkeys 
    DWORD    cbMaxSubKey;              // longest subkey size 
    DWORD    cchMaxClass;              // longest class string 
    DWORD    cValues;              // number of values for key 
    DWORD    cchMaxValue;          // longest value name 
    DWORD    cbMaxValueData;       // longest value data 
    DWORD    cbSecurityDescriptor; // size of security descriptor 
    FILETIME ftLastWriteTime;      // last write time 
    DWORD i, retCode; 
	
	char * buffer = NULL ;

	// On ouvre la clé
	if( RegOpenKeyEx( hMainKey, TEXT(lpSubKey), 0, KEY_READ, &hKey) != ERROR_SUCCESS ) return ;

    // Get the class name and the value count. 
    retCode = RegQueryInfoKey(
        hKey,                    // key handle 
        achClass,                // buffer for class name 
        &cchClassName,           // size of class string 
        NULL,                    // reserved 
        &cSubKeys,               // number of subkeys 
        &cbMaxSubKey,            // longest subkey size 
        &cchMaxClass,            // longest class string 
        &cValues,                // number of values for this key 
        &cchMaxValue,            // longest value name 
        &cbMaxValueData,         // longest value data 
        &cbSecurityDescriptor,   // security descriptor 
        &ftLastWriteTime);       // last write time 
 
	// Enumerate the subkeys, until RegEnumKeyEx fails.
	if (cSubKeys) {
		for (i=0; i<cSubKeys; i++) { 
			cbName = MAX_KEY_LENGTH;
			retCode = RegEnumKeyEx(hKey, i, achKey, &cbName, NULL, NULL, NULL, &ftLastWriteTime); 
			if (retCode == ERROR_SUCCESS) {
				buffer = (char*) malloc( strlen( TEXT(lpSubKey) ) + strlen( achKey ) + 100 ) ;
				sprintf( buffer, "[HKEY_CURRENT_USER\\%s\\%s]", TEXT(lpSubKey), achKey ) ;
				fprintf( fp_out, "\r\n%s\r\n", buffer ) ;
				if( text!=NULL ) 
					if( strlen( text ) > 0 ) fprintf( fp_out, "%s\r\n", text ) ;
				free( buffer );				
				}
			}
		} 
	RegCloseKey( hKey ) ;
	}

void InitRegistryAllSessions( HKEY hMainKey, LPCTSTR lpSubKey, char * SubKeyName, char * filename, char * text ) {
	FILE * fp;
	char buf[1024] = "" ;
	if( (fp=fopen( filename, "wb" )) != NULL ) {
		fprintf( fp, "Windows Registry Editor Version 5.00\r\n" ) ;
		sprintf( buf, "%s\\%s", lpSubKey, SubKeyName ); 
		QuerySubKey( hMainKey, (LPCTSTR)buf, fp, text ) ;
		fclose( fp ) ;
		}
	}
	
void InitAllSessions( HKEY hMainKey, LPCTSTR lpSubKey, char * SubKeyName, char * filename ) {
	char text[4096], f[1024] ;
	FILE * fp ;
	int len ;
	if( (fp=fopen(filename, "rb")) != NULL ) {
		len = fread( text, 1, 4096, fp ) ;
		fclose( fp ) ;
		text[4095]='\0'; text[len] = '\0' ;
		while( (text[strlen(text)-1]=='\n')||(text[strlen(text)-1]=='\r') ) text[strlen(text)-1]='\0' ;
		sprintf( f, "%s.reg", filename ) ;
		InitRegistryAllSessions( hMainKey, lpSubKey, SubKeyName, f, text ) ;
		unlink(filename);
		}
	}
	
// Détruit une valeur de clé de registre 
BOOL RegDelValue (HKEY hKeyRoot, LPTSTR lpSubKey, LPTSTR lpValue ) {
	HKEY hKey;
	LONG lResult;
	if( (lResult = RegOpenKeyEx (hKeyRoot, lpSubKey, 0, KEY_WRITE, &hKey)) == ERROR_SUCCESS ) {
		RegDeleteValue( hKey, lpValue ) ;
		RegCloseKey(hKey) ;
		}
	return TRUE;   
	}

// Detruit une clé de registre et ses sous-clé
BOOL RegDelTree (HKEY hKeyRoot, LPCTSTR lpSubKey) {
    TCHAR lpEnd[MAX_PATH];
    LONG lResult;
    DWORD dwSize;
    TCHAR szName[MAX_PATH];
    HKEY hKey;
    FILETIME ftWrite;

    // First, see if we can delete the key without having
    // to recurse.
    lResult = RegDeleteKey(hKeyRoot, lpSubKey);
    if (lResult == ERROR_SUCCESS) return TRUE;

    lResult = RegOpenKeyEx (hKeyRoot, lpSubKey, 0, KEY_READ, &hKey) ;

    if (lResult != ERROR_SUCCESS) {
        if (lResult == ERROR_FILE_NOT_FOUND) { printf("Key not found.\n"); return TRUE; } 
        else {printf("Error opening key.\n");return FALSE;}
    	}

    // Enumerate the keys
    dwSize = MAX_PATH;
    lResult = RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL, NULL, NULL, &ftWrite) ;

    if (lResult == ERROR_SUCCESS) 
    {
        do {
            //StringCchCopy (lpEnd, MAX_PATH*2, szName);
            sprintf(lpEnd, "%s\\%s", lpSubKey, szName);

            //if( !RegDelTree( hKeyRoot, lpSubKey ) ) { break ; }
            if( !RegDelTree( hKeyRoot, lpEnd ) ) { break ; }
            dwSize = MAX_PATH;
            lResult = RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL, NULL, NULL, &ftWrite) ;
        } while ( lResult == ERROR_SUCCESS ) ;
    }

	RegCloseKey(hKey) ;

	// Try again to delete the key.
	lResult = RegDeleteKey(hKeyRoot, lpSubKey);

	if (lResult == ERROR_SUCCESS) return TRUE;
	return FALSE;
	}

// Copie une clé de registre vers une autre
void RegCopyTree( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR lpDestKey ) { 
	HKEY hKey, hDestKey ;
    TCHAR    achKey[MAX_KEY_LENGTH];   // buffer for subkey name
    DWORD    cbName;                   // size of name string 
    TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
    DWORD    cchClassName = MAX_PATH;  // size of class string 
    DWORD    cSubKeys=0;               // number of subkeys 
    DWORD    cbMaxSubKey;              // longest subkey size 
    DWORD    cchMaxClass;              // longest class string 
    DWORD    cValues;              // number of values for key 
    DWORD    cchMaxValue;          // longest value name 
    DWORD    cbMaxValueData;       // longest value data 
    DWORD    cbSecurityDescriptor; // size of security descriptor 
    FILETIME ftLastWriteTime;      // last write time 
 
    DWORD i, retCode; 
 
    TCHAR  achValue[MAX_VALUE_NAME]; 
    DWORD cchValue = MAX_VALUE_NAME; 
	
	DWORD lpType, dwDataSize = 1024 ;
	char * buffer = NULL, * destbuffer = NULL ;
	
	// On ouvre la clé
	if( RegOpenKeyEx( hMainKey, TEXT(lpSubKey), 0, KEY_READ, &hKey) != ERROR_SUCCESS ) return ;
	if( RegCreateKey( hMainKey, TEXT(lpDestKey), &hDestKey ) == ERROR_SUCCESS )
					RegCloseKey( hDestKey ) ;

    // Get the class name and the value count. 
    retCode = RegQueryInfoKey(
        hKey,                    // key handle 
        achClass,                // buffer for class name 
        &cchClassName,           // size of class string 
        NULL,                    // reserved 
        &cSubKeys,               // number of subkeys 
        &cbMaxSubKey,            // longest subkey size 
        &cchMaxClass,            // longest class string 
        &cValues,                // number of values for this key 
        &cchMaxValue,            // longest value name 
        &cbMaxValueData,         // longest value data 
        &cbSecurityDescriptor,   // security descriptor 
        &ftLastWriteTime);       // last write time 
 
    // Enumerate the key values. 
    if (cValues) 
    {
        //printf( "\nNumber of values: %d\n", cValues);

        for (i=0, retCode=ERROR_SUCCESS; i<cValues; i++) 
        { 
            cchValue = MAX_VALUE_NAME; 
            achValue[0] = '\0'; 
            retCode = RegEnumValue(hKey, i, 
                achValue, 
                &cchValue, 
                NULL, 
                NULL,
                NULL,
                NULL);
 
            if (retCode == ERROR_SUCCESS ) 
            { 
				unsigned char lpData[1024] ;
				dwDataSize = 1024 ;
				RegQueryValueEx( hKey, TEXT( achValue ), 0, &lpType, lpData, &dwDataSize ) ;
				
				if( RegOpenKeyEx( hMainKey, TEXT(lpDestKey), 0, KEY_WRITE, &hDestKey) != ERROR_SUCCESS ) return ;
				
				RegSetValueEx( hDestKey, TEXT( achValue ), 0, lpType, lpData, dwDataSize );
					
				RegCloseKey( hDestKey ) ;
            } 
        }
    }
	
    // Enumerate the subkeys, until RegEnumKeyEx fails.
    if (cSubKeys)
    {
        //printf( "\nNumber of subkeys: %d\n", cSubKeys);

        for (i=0; i<cSubKeys; i++) 
        { 
            cbName = MAX_KEY_LENGTH;
            retCode = RegEnumKeyEx(hKey, i,
                     achKey, 
                     &cbName, 
                     NULL, 
                     NULL, 
                     NULL, 
                     &ftLastWriteTime); 
            if (retCode == ERROR_SUCCESS) 
            {
				buffer = (char*) malloc( strlen( TEXT(lpSubKey) ) + strlen( achKey ) + 3 ) ;
				sprintf( buffer, "%s\\%s", TEXT(lpSubKey), achKey ) ;
				destbuffer = (char*) malloc( strlen( TEXT(lpDestKey) ) + strlen( achKey ) + 3 ) ;
				sprintf( destbuffer, "%s\\%s", TEXT(lpDestKey), achKey ) ;
				if( RegCreateKey( hMainKey, destbuffer, &hDestKey ) == ERROR_SUCCESS )
					RegCloseKey( hDestKey ) ;
					
				RegCopyTree( hMainKey, buffer, destbuffer ) ;
				free( buffer );
				free( destbuffer );
            }
        }
    } 
 
	RegCloseKey( hKey ) ;
}

// Nettoie la clé de PuTTY pour enlever les clés et valeurs spécifique à KiTTY
BOOL RegCleanPuTTY( void ) {
	HKEY hKey, hSubKey ;
	DWORD retCode, i;
	TCHAR    achKey[MAX_KEY_LENGTH];   // buffer for subkey name
	DWORD    cbName;                   // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys=0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 
	char *buffer = NULL ;
#ifdef FDJ
return 1 ;
#endif
	if( (retCode = RegOpenKeyEx ( HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY", 0, KEY_WRITE, &hSubKey)) == ERROR_SUCCESS ) {
		RegDeleteValue( hSubKey, "Build" ) ;
		RegDeleteValue( hSubKey, "Folders" ) ;
		RegDeleteValue( hSubKey, "KiCount" ) ;
		RegDeleteValue( hSubKey, "KiLastSe" ) ;
		RegDeleteValue( hSubKey, "KiLastUH" ) ;
		RegDeleteValue( hSubKey, "KiLastUp" ) ;
		RegDeleteValue( hSubKey, "KiPath" ) ;
		RegDeleteValue( hSubKey, "KiSess" ) ;
		RegDeleteValue( hSubKey, "KiVers" ) ;
		RegDeleteValue( hSubKey, "CtHelperPath" ) ;
		RegDeleteValue( hSubKey, "PSCPPath" ) ;
		RegDeleteValue( hSubKey, "WinSCPPath" ) ;
		RegDeleteValue( hSubKey, "KiClassName" ) ;
		RegCloseKey(hSubKey) ;
		}
	
	RegDelTree (HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\Commands" ) ;
	RegDelTree (HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\Folders" ) ;
	RegDelTree (HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\Launcher" ) ;
	
	// On ouvre la clé
	if( RegOpenKeyEx( HKEY_CURRENT_USER, "Software\\SimonTatham\\PuTTY\\Sessions", 0, KEY_READ|KEY_WRITE, &hKey) != ERROR_SUCCESS ) return 0;
	
	retCode = RegQueryInfoKey(
        hKey,                    // key handle 
        achClass,                // buffer for class name 
        &cchClassName,           // size of class string 
        NULL,                    // reserved 
        &cSubKeys,               // number of subkeys 
        &cbMaxSubKey,            // longest subkey size 
        &cchMaxClass,            // longest class string 
        &cValues,                // number of values for this key 
        &cchMaxValue,            // longest value name 
        &cbMaxValueData,         // longest value data 
        &cbSecurityDescriptor,   // security descriptor 
        &ftLastWriteTime);
	
	// Enumerate the subkeys, until RegEnumKeyEx fails.
	if (cSubKeys) {  //printf( "\nNumber of subkeys: %d\n", cSubKeys);
		for (i=0; i<cSubKeys; i++) { 
			cbName = MAX_KEY_LENGTH;
			if( ( retCode = RegEnumKeyEx(hKey, i, achKey, &cbName,NULL,NULL,NULL, &ftLastWriteTime) ) == ERROR_SUCCESS ) {
				buffer = (char*) malloc( strlen( achKey ) + 50 ) ;
				sprintf( buffer, "Software\\SimonTatham\\PuTTY\\Sessions\\%s\\Commands", achKey ) ;
				RegDelTree( HKEY_CURRENT_USER, buffer );
				sprintf( buffer, "Software\\SimonTatham\\PuTTY\\Sessions\\%s", achKey ) ;
				if( (retCode = RegOpenKeyEx ( HKEY_CURRENT_USER, buffer, 0, KEY_WRITE, &hSubKey)) == ERROR_SUCCESS ) {
					RegDeleteValue( hSubKey, "BCDelay" ) ;
					RegDeleteValue( hSubKey, "BgOpacity" ) ;
					RegDeleteValue( hSubKey, "BgSlideshow" ) ;
					RegDeleteValue( hSubKey, "BgType" ) ;
					RegDeleteValue( hSubKey, "BgImageFile" ) ;
					RegDeleteValue( hSubKey, "BgImageStyle" ) ;
					RegDeleteValue( hSubKey, "BgImageAbsoluteX" ) ;
					RegDeleteValue( hSubKey, "BgImageAbsoluteY" ) ;
					RegDeleteValue( hSubKey, "BgImagePlacement" ) ;
					RegDeleteValue( hSubKey, "Fullscreen" ) ;
					RegDeleteValue( hSubKey, "Maximize" ) ;
					RegDeleteValue( hSubKey, "SendToTray" ) ;
					RegDeleteValue( hSubKey, "SaveOnExit" ) ;
					RegDeleteValue( hSubKey, "Folder" ) ;
					RegDeleteValue( hSubKey, "Icone" ) ;
					RegDeleteValue( hSubKey, "IconeFile" ) ;
					RegDeleteValue( hSubKey, "SFTPConnect" ) ;
					RegDeleteValue( hSubKey, "InitDelay" ) ;
					RegDeleteValue( hSubKey, "Password" ) ;
					RegDeleteValue( hSubKey, "Autocommand" ) ;
					RegDeleteValue( hSubKey, "AutocommandOut" ) ;
					RegDeleteValue( hSubKey, "AntiIdle" ) ;
					RegDeleteValue( hSubKey, "LogTimestamp" ) ;
					RegDeleteValue( hSubKey, "Notes" ) ;
					RegDeleteValue( hSubKey, "CygtermCommand" ) ;
					RegDeleteValue( hSubKey, "CygtermAltMetabit" ) ;
					RegDeleteValue( hSubKey, "CygtermAutoPath" ) ;
					RegDeleteValue( hSubKey, "Cygterm64" ) ;
					RegDeleteValue( hSubKey, "WakeupReconnect" ) ;
					RegDeleteValue( hSubKey, "FailureReconnect" ) ;
					RegDeleteValue( hSubKey, "Scriptfile" ) ;
					RegDeleteValue( hSubKey, "ScriptfileContent" ) ;
					RegDeleteValue( hSubKey, "TransparencyValue" ) ;
					RegDeleteValue( hSubKey, "TermXPos" ) ;
					RegDeleteValue( hSubKey, "TermYPos" ) ;
					RegDeleteValue( hSubKey, "AuthPKCS11" ) ;
					RegDeleteValue( hSubKey, "PKCS11LibFile" ) ;
					RegDeleteValue( hSubKey, "PKCS11TokenLabel" ) ;
					RegDeleteValue( hSubKey, "PKCS11CertLabel" ) ;
					RegDeleteValue( hSubKey, "CopyURLDetection" ) ;
					RegDeleteValue( hSubKey, "HyperlinkUnderline" ) ;
					RegDeleteValue( hSubKey, "HyperlinkUseCtrlClick" ) ;
					RegDeleteValue( hSubKey, "HyperlinkBrowserUseDefault" ) ;
					RegDeleteValue( hSubKey, "HyperlinkBrowser" ) ;
					RegDeleteValue( hSubKey, "HyperlinkRegularExpressionUseDefault" ) ;
					RegDeleteValue( hSubKey, "HyperlinkRegularExpression" ) ;
					RegDeleteValue( hSubKey, "rzCommand" ) ;
					RegDeleteValue( hSubKey, "rzOptions" ) ;
					RegDeleteValue( hSubKey, "szCommand" ) ;
					RegDeleteValue( hSubKey, "szOptions" ) ;
					RegDeleteValue( hSubKey, "zDownloadDir" ) ;
					RegDeleteValue( hSubKey, "SaveWindowPos" ) ;
					RegDeleteValue( hSubKey, "WindowState" ) ;
					RegDeleteValue( hSubKey, "ForegroundOnBell" ) ;
					RegDeleteValue( hSubKey, "CtrlTabSwitch" ) ;
					RegDeleteValue( hSubKey, "Comment" ) ;
					RegDeleteValue( hSubKey, "LogTimeRotation" ) ;
					RegDeleteValue( hSubKey, "PortKnocking" ) ;
					RegDeleteValue( hSubKey, "WindowClosable" ) ;
					RegDeleteValue( hSubKey, "WindowMinimizable" ) ;
					RegDeleteValue( hSubKey, "WindowMaximizable" ) ;
					RegDeleteValue( hSubKey, "WindowHasSysMenu" ) ;
					RegDeleteValue( hSubKey, "DisableBottomButtons" ) ;
					RegDeleteValue( hSubKey, "BoldAsColour" ) ;
					RegDeleteValue( hSubKey, "UnderlinedAsColour" ) ;
					RegDeleteValue( hSubKey, "SelectedAsColour" ) ;
					RegDeleteValue( hSubKey, "ScriptFileName" ) ;
					RegDeleteValue( hSubKey, "ScriptMode" ) ;
					RegDeleteValue( hSubKey, "ScriptLineDelay" ) ;
					RegDeleteValue( hSubKey, "ScriptCharDelay" ) ;
					RegDeleteValue( hSubKey, "ScriptCondLine" ) ;
					RegDeleteValue( hSubKey, "ScriptCondUse" ) ;
					RegDeleteValue( hSubKey, "ScriptCRLF" ) ;
					RegDeleteValue( hSubKey, "ScriptEnable" ) ;
					RegDeleteValue( hSubKey, "ScriptExcept" ) ;
					RegDeleteValue( hSubKey, "ScriptTimeout" ) ;
					RegDeleteValue( hSubKey, "ScriptWait" ) ;
					RegDeleteValue( hSubKey, "ScriptHalt" ) ;
					//RegDeleteValue( hSubKey, "" ) ;
 					RegCloseKey(hSubKey) ;
					}
				free( buffer );
				}
			}
		} 
 
	RegCloseKey( hKey ) ;
	
	return 1;
	}

// Creation du SSH Handler
void CreateSSHHandler() {
	char path[1024], buffer[1024] ;

	GetModuleFileName( NULL, (LPTSTR)path, 1024 ) ;

	// Telnet
	RegTestOrCreate( HKEY_CLASSES_ROOT, "telnet", "", "URL:Telnet Protocol") ;
	RegTestOrCreateDWORD( HKEY_CLASSES_ROOT, "telnet", "EditFlags", 2) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "telnet", "FriendlyTypeName", "@ieframe.dll,-907") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "telnet", "URL Protocol", "") ;
	RegTestOrCreateDWORD( HKEY_CLASSES_ROOT, "telnet", "BrowserFlags", 8) ;

	sprintf(buffer, "%s,0", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "telnet\\DefaultIcon", "", buffer ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "telnet\\shell", "", "") ;

	sprintf(buffer, "\"%s\" %%1", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "telnet\\shell\\open\\command", "", buffer ) ;

	// SSH
	RegTestOrCreate( HKEY_CLASSES_ROOT, "ssh", "", "URL:SSH Protocol") ;
	RegTestOrCreateDWORD( HKEY_CLASSES_ROOT, "ssh", "EditFlags", 2) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "ssh", "FriendlyTypeName", "@ieframe.dll,-907") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "ssh", "URL Protocol", "") ;
	RegTestOrCreateDWORD( HKEY_CLASSES_ROOT, "ssh", "BrowserFlags", 8) ;

	sprintf(buffer, "%s,0", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "ssh\\DefaultIcon", "", buffer ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "ssh\\shell", "", "") ;

	sprintf(buffer, "\"%s\" %%1", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "ssh\\shell\\open\\command", "", buffer ) ;

	// PuTTY
	RegTestOrCreate( HKEY_CLASSES_ROOT, "putty", "", "URL:PuTTY Protocol") ;
	RegTestOrCreateDWORD( HKEY_CLASSES_ROOT, "putty", "EditFlags", 2) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "putty", "FriendlyTypeName", "@ieframe.dll,-907") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "putty", "URL Protocol", "") ;
	RegTestOrCreateDWORD( HKEY_CLASSES_ROOT, "putty", "BrowserFlags", 8) ;

	sprintf(buffer, "%s,0", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "putty\\DefaultIcon", "", buffer ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "putty\\shell", "", "") ;

	sprintf(buffer, "\"%s\" -load \"%%1\"", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "putty\\shell\\open\\command", "", buffer ) ;
		}

// Creation de l'association de fichiers *.ktx
void CreateFileAssoc() {
	char path[1024], buffer[1024] ;

	GetModuleFileName( NULL, (LPTSTR)path, 1024 ) ;

	// Association des fichers .ktx avec l'application KiTTY
	// Création d l'application
	RegTestOrCreate( HKEY_CLASSES_ROOT, "kitty.connect.1", "", "KiTTY connection manager") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "kitty.connect.1", "FriendlyTypeName", "@KiTTY, -120") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "kitty.connect.1\\CurVer", "", "kitty.connect.1") ;
	sprintf(buffer, "%s", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "kitty.connect.1\\DefaultIcon", "", buffer);
	sprintf(buffer, "\"%s\" -kload \"%%1\"", path ) ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, "kitty.connect.1\\shell\\open\\command", "", buffer) ;
	// Création de l'association de fichiers
	RegTestOrCreate( HKEY_CLASSES_ROOT, ".ktx", "", "kitty.connect.1") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, ".ktx", "PerceivedType", "Connection") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, ".ktx", "Content Type", "connection/ssh") ;
	RegTestOrCreate( HKEY_CLASSES_ROOT, ".ktx", "OpenWithProgids", "kitty.connect.1") ;
}
	
// Vérifie l'existance de la clé de KiTTY sinon la copie depuis PuTTY
void TestRegKeyOrCopyFromPuTTY( HKEY hMainKey, char * KeyName ) { 
	HKEY hKey ;
	if( RegOpenKeyEx( hMainKey, TEXT(KeyName), 0, KEY_READ, &hKey) == ERROR_SUCCESS ) {
		RegCloseKey( hKey ) ;
		}
	else {
		RegCreateKey( hMainKey, TEXT(KeyName), &hKey ) ;
		RegCloseKey( hKey ) ;
#ifndef FDJ
		RegCopyTree( hMainKey, "Software\\SimonTatham\\PuTTY", TEXT(KeyName) ) ;
#endif
		}
	}


	
/******
Supprimer toute trace de KiTTY dans le registre.
Ecrire et exécuter un fichier utf-8 .reg contenant les lignes:

Windows Registry Editor Version 5.00

[-HKEY_CURRENT_USER\Software\9bis.com\KiTTY]

[-HKEY_CLASSES_ROOT\telnet]

[-HKEY_CLASSES_ROOT\ssh]

[-HKEY_CLASSES_ROOT\putty]

[-HKEY_CLASSES_ROOT\kitty.connect.1]

[-HKEY_CLASSES_ROOT\.ktx]

******/
