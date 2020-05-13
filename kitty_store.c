#include <limits.h>


#include "kitty_store.h"
#include "kitty_commun.h"
#include "kitty_tools.h"


#ifndef snewn
#define snewn(n, type) ((type *)safemalloc((n), sizeof(type)))
#endif
#ifndef sfree
#define sfree safefree
#endif

void debug_log( const char *fmt, ... ) ;


void DelDir( const char * directory ) ;
BOOL RegDelTree (HKEY hKeyRoot, LPCTSTR lpSubKey) ;
void CleanFolderName( char * folder ) ;
char * GetConfigDirectory( void ) ;
int GetReadOnlyFlag(void) ;

char * ltoa (long int __val, char *__s, int __radix) ;
char * itoa (int __val, char *__s, int __radix)  ;
const char *win_strerror(int error) ;
char seedpath[2 * MAX_PATH + 10] = "\0";
char sesspath[2 * MAX_PATH] = "\0";
char initialsesspath[2 * MAX_PATH] = "\0";
char sshkpath[2 * MAX_PATH] = "\0";
char jumplistpath[2 * MAX_PATH] = "\0";
char oldpath[2 * MAX_PATH] = "\0";
char sessionsuffix[16] = "\0";
char keysuffix[16] = "\0";

static const char hex[16] = "0123456789ABCDEF";

void fatal_error(const char *p, ...) { exit(0) ; }
/*
void *safemalloc(size_t n, size_t size) {
    void *p;

    if (n > INT_MAX / size) {
	p = NULL;
    } else {
	size *= n;
	if (size == 0) size = 1;
#ifdef MINEFIELD
	p = minefield_c_malloc(size);
#else
	p = malloc(size);
#endif
    }

    if (!p)
       fatal_error("out of memory");

    return p;
}
void safefree(void *ptr)
{
    if (ptr) {
#ifdef MINEFIELD
	minefield_c_free(ptr);
#else
	free(ptr);
#endif
    }
}
*/

void SetHostKeyExtension( const char* ext ) {
	char * buffer ;
	buffer = (char*)malloc(strlen(ext)+2);
	if( ext[0]!='.' ) { strcpy( buffer, "." ) ; } else { strcpy( buffer, "" ) ; }
	strcat( buffer, ext ) ;
	while( buffer[strlen(buffer)-1]==' ' ) { buffer[strlen(buffer)-1] = '\0' ; }
	if( strlen(buffer)>15 ) { buffer[15]='\0' ; }
	strcpy( keysuffix, buffer ) ;
	free( buffer ) ;
}


/* JK: my generic function for simplyfing error reporting */
DWORD errorShow(const char* pcErrText, const char* pcErrParam) {
	HWND hwRodic;
	DWORD erChyba;
	char pcBuf[16];
	char* pcHlaska = snewn((pcErrParam?strlen(pcErrParam):0) + strlen(pcErrText) + 256, char);
	
	erChyba = GetLastError();		
	ltoa(erChyba, pcBuf, 10);

	strcpy(pcHlaska, "Error: ");
	strcat(pcHlaska, pcErrText);
	strcat(pcHlaska, "\n");
	strcat(pcHlaska, "Directory: ");
	char currpath[2*MAX_PATH];
	GetCurrentDirectory( (MAX_PATH*2), currpath);
	strcat(pcHlaska, currpath);
	strcat(pcHlaska, "\n");

	if (pcErrParam) {
		strcat(pcHlaska, pcErrParam);
		strcat(pcHlaska, "\n");
	}
    strcat(pcHlaska, "Error code: ");
	strcat(pcHlaska, pcBuf);

    /* JK: get parent-window and show */
    hwRodic = GetActiveWindow();
    if (hwRodic != NULL) { hwRodic = GetLastActivePopup(hwRodic);}
  
	if (MessageBox(hwRodic, pcHlaska, "Error", MB_OK|MB_APPLMODAL|MB_ICONEXCLAMATION) == 0) {
        /* JK: this is really bad -> just ignore */
	sfree(pcHlaska);
        return 0;
    }
    
    //MessageBox(GetActiveWindow(),pcHlaska, "Error", MB_OK|MB_APPLMODAL|MB_ICONEXCLAMATION) ;
    

	sfree(pcHlaska);
	return erChyba;
};

/* JK: pack string for use as filename - pack < > : " / \ | */
void packstr(const char *in, char *out) {
EMERGENCY_INIT
    while (*in) {
EMERGENCY_BREAK
		if (*in == '<' || *in == '>' || *in == ':' || *in == '"' ||
	    *in == '/' || *in == '|') {
	    *out++ = '%';
	    *out++ = hex[((unsigned char) *in) >> 4];
	    *out++ = hex[((unsigned char) *in) & 15];
	} else
	    *out++ = *in;
	in++;
    }
    *out = '\0';
    return;
}

/*
 * JK: create directory if specified as dir1\dir2\dir3 and dir1|2 doesn't exists
 * handle if part of path already exists
*/
int createPath(char* dir) {
    char *p;
	if( GetReadOnlyFlag() ) { return 1 ; }
	p = strrchr(dir, '\\');

	if (p == NULL) {
		/* what if it already exists */
		if (!SetCurrentDirectory(dir)) {
			CreateDirectory(dir, NULL);
			return SetCurrentDirectory(dir);
		}
		return 1;
	}
	
	*p = '\0';
	if( !createPath(dir) ) { MessageBox(NULL,"Unable to create directory !","Error",MB_OK|MB_ICONERROR) ; }
	*p = '\\';
	++p;
	/* what if it already exists */
	if (!SetCurrentDirectory(dir)) {
		CreateDirectory(p, NULL);
		return SetCurrentDirectory(p) ;
	}
	return 1;
}

/*
 * JK: join path pcMain.pcSuf solving extra cases to pcDest
 * expecting - pcMain as path from WinAPI ::GetCurrentDirectory()/GetModuleFileName()
 *           - pcSuf as user input path from config (at least MAX_PATH long)
*/
char* joinPath(char* pcDest, char* pcMain, char* pcSuf) {

	char* pcBuf = snewn(MAX_PATH+1, char);

	/* at first ExpandEnvironmentStrings */
	if (0 == ExpandEnvironmentStrings(pcSuf, pcBuf, MAX_PATH)) {
		/* JK: failure -> revert back - but it ussualy won't work, so report error to user! */
		errorShow("Unable to ExpandEnvironmentStrings for session path", pcSuf);
		strncpy(pcBuf, pcSuf, strlen(pcSuf));
	}
	/* now ExpandEnvironmentStringsForUser - only on win2000Pro and above */
	/* It's much more tricky than I've expected, so it's ToDo */
	/*
	static HMODULE userenv_module = NULL;
	typedef BOOL (WINAPI *p_ExpandESforUser_t) (HANDLE, LPCTSTR, LPTSTR, DWORD);
	static p_ExpandESforUser_t p_ExpandESforUser = NULL;
	
	HMODULE userenv_module = LoadLibrary("USERENV.DLL");

	if (userenv_module) {
	    p_ExpandESforUser = (p_ExpandESforUser_t) GetProcAddress(shell32_module, "ExpandEnvironmentStringsForUserA");
		
		if (p_ExpandESforUser) {

			TOKEN_IMPERSONATE

			if (0 == (p_ExpandESforUser(NULL, pcSuf, pcBuf,	MAX_PATH))) {
	    		*//* JK: failure -> revert back - but it ussualy won't work, so report error to user! *//*
				errorShow("Unable to ExpandEnvironmentStringsForUser for session path", pcBuf);
				strncpy(pcSuf, pcBuf, strlen(pcSuf));
			}
		}
	}*/

	/* expand done, resutl in pcBuf */

	if ((*pcBuf == '/') || (*pcBuf == '\\')) {
		/* everything ok */
		strcpy(pcDest, pcMain);
		strcat(pcDest, pcBuf);
	}
	else {
		if (*(pcBuf+1) == ':') {
			/* absolute path */
			strcpy(pcDest, pcBuf);
		}
		else {
			/* some weird relative path - add '\' */
			strcpy(pcDest, pcMain);
			strcat(pcDest, "\\");
			strcat(pcDest, pcBuf);
		}
	}
	sfree(pcBuf);
	return pcDest;
}

/*
 * JK: init path variables from config or otherwise
 * as of 1.5 GetModuleFileName solves our currentDirectory problem
*/
int loadPath() {
	if( *sesspath != '\0') { return 0 ; }

	char *fileCont = NULL;
	DWORD fileSize;
	DWORD bytesRead;
	char *p = NULL;
	char *p2 = NULL;
	HANDLE hFile ;

	char* puttypath = snewn( (MAX_PATH*2), char);
	
	/* JK:  save path/curdir */
	GetCurrentDirectory( (MAX_PATH*2), oldpath);

	/* JK: try curdir for putty.conf first */
	hFile = CreateFile("putty.conf",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		/* JK: there is a putty.conf in curdir - use it and use curdir as puttypath */
		GetCurrentDirectory( (MAX_PATH*2), puttypath);
		CloseHandle(hFile);
	} else {
		/* JK: get where putty.exe is */
		if (GetModuleFileName(NULL, puttypath, (MAX_PATH*2)) != 0)
		{
			p = strrchr(puttypath, '\\');
			if (p)
			{
				*p = '\0';
			}
			SetCurrentDirectory(puttypath);
		}
		else GetCurrentDirectory( (MAX_PATH*2), puttypath);
	}

	/* JK: set default values - if there is a config file, it will be overwitten */
	if( GetConfigDirectory() != NULL ) { // Cas ou defini un autre repertoire de configuration
		strcpy(sesspath, GetConfigDirectory());
		strcat(sesspath, "\\Sessions");
		strcpy(initialsesspath,sesspath);
		strcpy(sshkpath, GetConfigDirectory());
		strcat(sshkpath, "\\SshHostKeys");
		strcpy(jumplistpath, GetConfigDirectory());
		strcat(jumplistpath, "\\Jumplist");
		strcpy(seedpath, GetConfigDirectory());
		strcat(seedpath, "\\putty.rnd");
		}
	else {
		strcpy(sesspath, puttypath);
		strcat(sesspath, "\\Sessions");
		strcpy(initialsesspath,sesspath);
		strcpy(sshkpath, puttypath);
		strcat(sshkpath, "\\SshHostKeys");
		strcpy(jumplistpath, puttypath);
		strcat(jumplistpath, "\\Jumplist");
		strcpy(seedpath, puttypath);
		strcat(seedpath, "\\putty.rnd");
		}

	hFile = CreateFile("putty.conf",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	/* Test Sessions directory */
	if(get_param("INIFILE")==SAVEMODE_DIR)
	if( !existdirectory(sesspath) ) {
		if( !MakeDir(sesspath) ) {
			MessageBox( NULL, "Unable to create sessions directory !", "Error", MB_OK|MB_ICONERROR ) ;
		}
	}

	/* JK: now we can pre-clean-up */
	SetCurrentDirectory(oldpath);

	if (hFile != INVALID_HANDLE_VALUE) {
		fileSize = GetFileSize(hFile, NULL);
		fileCont = snewn(fileSize+16, char);

		if (!ReadFile(hFile, fileCont, fileSize, &bytesRead, NULL))
		{
			errorShow("Unable to read configuration file, falling back to defaults", NULL);
			/* JK: default values are already there and clean-up at end */
		}
		else {
			/* JK: parse conf file to path variables */
			*(fileCont+fileSize+1) = '\0';
			*(fileCont+fileSize) = '\n';
			p = fileCont;
EMERGENCY_INIT
			while (p) {
EMERGENCY_BREAK
				if (*p == ';') {	/* JK: comment -> skip line */
					p = strchr(p, '\n');
					++p;
					continue;
				}
				p2 = strchr(p, '=');
				if (!p2) break;
				*p2 = '\0';
				++p2;

				if (!strcmp(p, "Sessions")) {
					p = strchr(p2, '\n');
					*p = '\0';
					joinPath(sesspath, puttypath, p2);
					p2 = sesspath+strlen(sesspath)-1;
					while ((*p2 == ' ')||(*p2 == '\n')||(*p2 == '\r')||(*p2 == '\t')) --p2;
					*(p2+1) = '\0';
				}
				else if (!strcmp(p, "SshHostKeys")) {
					p = strchr(p2, '\n');
					*p = '\0';
					joinPath(sshkpath, puttypath, p2);
					p2 = sshkpath+strlen(sshkpath)-1;
					while ((*p2 == ' ')||(*p2 == '\n')||(*p2 == '\r')||(*p2 == '\t')) --p2;
					*(p2+1) = '\0';
				}
				else if (!strcmp(p, "Jumplist")) {
					p = strchr(p2, '\n');
					*p = '\0';
					joinPath(jumplistpath, puttypath, p2);
					p2 = jumplistpath+strlen(jumplistpath)-1;
					while ((*p2 == ' ')||(*p2 == '\n')||(*p2 == '\r')||(*p2 == '\t')) --p2;
					*(p2+1) = '\0';
				}
				else if (!strcmp(p, "seedfile")) {
					p = strchr(p2, '\n');
					*p = '\0';
					joinPath(seedpath, puttypath, p2);			
					p2 = seedpath+strlen(seedpath)-1;
					while ((*p2 == ' ')||(*p2 == '\n')||(*p2 == '\r')||(*p2 == '\t')) --p2;
					*(p2+1) = '\0';
				}
				else if (!strcmp(p, "sessionsuffix")) {
					p = strchr(p2, '\n');
					*p = '\0';
					strcpy(sessionsuffix, p2);
					p2 = sessionsuffix+strlen(sessionsuffix)-1;
					while ((*p2 == ' ')||(*p2 == '\n')||(*p2 == '\r')||(*p2 == '\t')) --p2;
					*(p2+1) = '\0';
				}
				else if (!strcmp(p, "keysuffix")) {
					p = strchr(p2, '\n');
					*p = '\0';
					strcpy(keysuffix, p2);
					p2 = keysuffix+strlen(keysuffix)-1;
					while ((*p2 == ' ')||(*p2 == '\n')||(*p2 == '\r')||(*p2 == '\t')) --p2;
					*(p2+1) = '\0';
				}
				++p;
			}
		}
		CloseHandle(hFile);
		sfree(fileCont);
	}
	/* else - INVALID_HANDLE {
		 * JK: unable to read conf file - probably doesn't exists
		 * we won't create one, user wants putty light, just fall back to defaults
		 * and defaults are already there
	}*/

	sfree(puttypath);
	return 1;
}
char * SetSessPath( const char * dec ) {
	int i ;
	char *pst ;
	if( !strcmp(dec,"..") ) {
		if( strcmp(sesspath, initialsesspath) ) {
			i=strlen(sesspath)-1 ;
			while( (sesspath[i]!='\\') && (i>0) ) i--;
			sesspath[i]='\0';
			}
		}
	else {
		strcat( sesspath, "\\" ) ;
		strcat( sesspath, dec ) ;
		}
	CleanFolderName( sesspath ) ;
	pst = sesspath+strlen(initialsesspath ) ;
	while( pst[0]=='\\' ) pst++ ;
	return pst ;
	}
	
char * SetInitialSessPath( void ) { return strcpy( sesspath, initialsesspath ) ; }

char * GetSessPath( void ) {
	return sesspath ;
}
	
int CreateFolderInPath( const char * d ) {
	char buf[MAX_PATH] ;
	int res = 0 ;
	sprintf( buf, "%s\\%s", sesspath, d ) ;
	res = createPath( buf ) ;
	if( !res ) { MessageBox(NULL,"Unable to create directory", "Error", MB_OK|MB_ICONERROR); }
	return res ;
}

void SaveDumpPortableConfig( FILE * fp ) {
	fprintf( fp, "seedpath=%s\n", seedpath ) ;
	fprintf( fp, "sesspath=%s\n", sesspath ) ;
	fprintf( fp, "initialsesspath=%s\n", initialsesspath ) ;
	fprintf( fp, "sshkpath=%s\n", sshkpath ) ;
	fprintf( fp, "jumplistpath=%s\n", jumplistpath ) ;
	fprintf( fp, "oldpath=%s\n", oldpath ) ;
	fprintf( fp, "sessionsuffix=%s\n", sessionsuffix ) ;
	fprintf( fp, "keysuffix=%s\n", keysuffix ) ;
}


HSettingsItem SettingsNewItem( const char * name, const char * value ) {
	if( name==NULL ) return NULL ; 
	HSettingsItem NewItem = malloc( sizeof( SettingsItem ) ) ;
	NewItem->pNext = NULL ;
	NewItem->pPrevious = NULL ;
	NewItem->name = malloc( strlen(name) + 1 ) ; strcpy( NewItem->name, name ) ;
	if( value == NULL ) { 
		NewItem->value = NULL ;
	} else {
		NewItem->value = malloc( strlen(value) + 1 ) ; strcpy( NewItem->value, value ) ;
	}
	return NewItem ;
}

HSettingsList SettingsInit() {
	HSettingsList list = (HSettingsList)malloc( sizeof(SettingsList) ) ;
	list->filename = NULL ;
	list->num = 0 ;
	list->first = NULL ;
	list->last = NULL ;
	return list ;
}

HSettingsList PortableSettings ;

void SettingsDelItem( HSettingsList list, const char * key ) {
	if( list != NULL ) {
		HSettingsItem current = list->first ;
		while( current != NULL ) {
			if( current->name != NULL ) {
				if( !strcmp( current->name, key ) ) {
					if( current->value != NULL ) { free( current->value ) ; current->value = NULL ; }
					free( current->name ) ; current->name = NULL ;
					if( current->pPrevious != NULL ) { 
						current->pPrevious->pNext = current->pNext ;
					} else {
						list->first = current->pNext ;
					}
					if( current->pNext != NULL )	{
						current->pNext->pPrevious = current->pPrevious ;
					} else {
						list->last = current->pPrevious ;
					}
				} 
			}
			current = current->pNext ;
		}
	}
}

void SettingsAddItem( HSettingsList list, const char * name, const char * value ) {
	if( list!=NULL ) {
		HSettingsItem NewItem = SettingsNewItem( name, value ) ;
		if( NewItem != NULL ) {
			HSettingsItem current = list->last ;
			if( current == NULL ) {
				list->first = NewItem ;
				NewItem->pNext = NULL ;
				NewItem->pPrevious = NULL ;
			} else {
				SettingsDelItem( list, name ) ;
				current->pNext = NewItem ;
				NewItem->pPrevious = current ;
			}
			list->last = NewItem ;
			list->num = list->num + 1 ;
		}
	}
}

void SettingsFreeItem( HSettingsItem item ) {
	if( item != NULL ) {
//debug_log("%s=%s\n",item->name,item->value);
		if( item->value != NULL ) { free( item->value ) ; item->value = NULL ; } // POURQUOI CA PLANTE ???
		if( item->name != NULL ) { free( item->name ) ; item->name = NULL ; }
		item->pPrevious = NULL ;
		item->pNext = NULL ;
		free( item ) ;
	}
}

void SettingsFree( HSettingsList list ) {
	if( list != NULL ) {
		HSettingsItem current = list->first, next ;
		if( current != NULL ) {
			while( current != NULL ) {
				next = current->pNext ;
				SettingsFreeItem( current ) ;
				current = next ;
			} 
		}
		list->first = NULL ;
		list->last = NULL ;
		if( list->filename != NULL ) { free( list->filename ) ; list->filename = NULL ; }
		list->num = 0 ;
		free( list ) ;
	}
}

char * SettingsKey( HSettingsList list, const char * key ) {
	if( list != NULL ) {
		HSettingsItem current = list->first ;
		while( current != NULL ) {
			if( current->name != NULL ) {
				if( !strcmp( current->name, key ) ) return current->value ;
			}
			current = current->pNext ;
		}
	}
	return NULL ;
}

char * SettingsKey_str( HSettingsList list, const char * key ) {
	if( list != NULL ) {
		HSettingsItem current = list->first ;
		while( current != NULL ) {
			if( current->name != NULL ) {
				if( !strcmp( current->name, key ) ) {
					return dupstr( current->value ) ;
				}
			}
			current = current->pNext ;
		}
	}
	return NULL ;
}

int SettingsKey_int( HSettingsList list, const char * key, const int defvalue ) {
	if( list != NULL ) {
		HSettingsItem current = list->first ;
		while( current != NULL ) {
			if( current->name != NULL ) {
				if( !strcmp( current->name, key ) ) {
					return atoi( current->value ) ;
				}
			}
			current = current->pNext ;
		}
	}
	return defvalue ;
}

void SettingsLoad( HSettingsList list, const char * filename ) {
	FILE * fp ;
	char buffer[4096] ;
	int p ;
	
//debug_log("filename=%s|\n",filename); int i=0;
	
	if( (fp=fopen(filename,"rb")) != NULL ) {
		list->filename = (char*) malloc( strlen(filename)+1 ) ; strcpy( list->filename, filename ) ;
		while( fgets(buffer,4096,fp) != NULL ) {
//debug_log("\nline %05d[%d]: %s|\n",++i,strlen(buffer),buffer);
			char *name, *value, *value2 ;
			while( (buffer[strlen(buffer)-1]=='\n') || (buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1] = '\0' ;
//debug_log("line %05d[%d]: %s|\n",i,strlen(buffer),buffer);
//debug_log("\t-2=%c -3=%c\n",buffer[strlen(buffer)-2],buffer[strlen(buffer)-3]);
			while( buffer[strlen(buffer)-1]!='\\' ) {
//debug_log("ici\n");
				while( buffer[strlen(buffer)-1]=='\r' ) { buffer[strlen(buffer)+1]='\0' ; buffer[strlen(buffer)-1]='\\' ; buffer[strlen(buffer)] = 'r' ; }
				while( buffer[strlen(buffer)-1]=='\n' ) { buffer[strlen(buffer)+1]='\0' ; buffer[strlen(buffer)-1]='\\' ; buffer[strlen(buffer)] = 'n' ; }
				if( fgets( buffer+strlen(buffer), 4096, fp ) == NULL ) break ;
				while( (buffer[strlen(buffer)-1]=='\n') || (buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1] = '\0' ;
			}
//debug_log("line %05d[%d]: %s|\n",i,strlen(buffer),buffer);
			while( (buffer[strlen(buffer)-1]=='\n') || (buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1] = '\0' ;
//debug_log("line %05d[%d]: %s|\n",i,strlen(buffer),buffer);
			if( buffer[strlen(buffer)-1] != '\\' ) { strcat( buffer, "\\" ) ; }
//debug_log("line %05d[%d]: %s|\n",i,strlen(buffer),buffer);
			p = poss( "\\", buffer ) ;
			if( p>1 ) {
				name = (char*) malloc( p+1 ) ;
				memcpy( name, buffer, p-1 ) ; name[p-1]='\0' ;
				value = (char*) malloc( strlen(buffer)-p+1 ) ;
				strcpy( value, buffer+p ) ;
				value[strlen(value)-1]='\0' ;
//debug_log("line %05d: %s|%s|\n",i,name,value);
				value2 = (char*) malloc( strlen(value)+1 ) ;
				unmungestr( value, value2, strlen(value)+1 ) ;
//debug_log("line %05d: %s|%s|%s|\n",i,name,value,value2);
				SettingsAddItem( list, name, value2 ) ;
				free( value2 ) ;
				free( value ) ;
				free( name ) ;
			}

		}
		fclose(fp );
	} else {
		//if( strcmp(filename,"Default%20Settings") ) MessageBox(NULL,"Unable to open session file", "Error", MB_OK);
		errorShow( "Unable to read session file", filename ) ;
	}
}

void SettingsSave( HSettingsList list, const char * filename ) {
	FILE * fp ;
	char buffer[4096] ;
	
	if( (fp=fopen(filename,"wb")) != NULL ) {
		if( list != NULL ) {
			HSettingsItem current = list->first ;
			while( current != NULL ) {
				if( current->name != NULL ) {
					if( current->value == NULL ) {
						sprintf( buffer, "%s\\\\\n", current->value ) ;
					} else {
						char * p = (char*) malloc( 3*strlen(current->value)+1 ) ;
						mungestr( current->value, p ) ;
						sprintf( buffer, "%s\\%s\\\n", current->name, p ) ;
						free( p ) ;
					}
					fputs( buffer, fp ) ;
					fflush( fp ) ;
				}
				current = current->pNext ;
			}
		}
		fclose(fp);
	} else {
		errorShow( "Unable to write session file", filename ) ;
	}
}


/*
void SettingsPrint( HSettingsList list ) {
	if( list != NULL ) {
		debug_log( "->filename=%s\n", list->filename ) ;
		debug_log( "->num=%d\n", list->num ) ;
		debug_log( "->first=%ld\n", list->first ) ;
		debug_log( "->last=%ld\n", list->last ) ;
		HSettingsItem current = list->first ;
		while( current != NULL ) {
			if( current->name !=NULL ) {
				if( current->value !=NULL ) { debug_log( "%s=%s\n", current->name, current->value ) ;
				} else { debug_log( "%s=NULL\n", current->name ) ;
				}
			}
			debug_log("	current=%ld\n", current );
			if( current->pPrevious == NULL ) { debug_log( "	previous=NULL\n" ) ; }
			else { debug_log( "	previous=%ld\n", current->pPrevious ) ; }
			if( current->pNext == NULL ) { debug_log( "	next=NULL\n" ) ; }
			else { debug_log( "	next=%ld\n", current->pNext ) ; }
			current = current->pNext ;
		}
	}
	debug_log( "NULL\n\n" ) ;
}

void SettingTest( void ) {
	PortableSettings = SettingsInit() ;
	SettingsPrint( PortableSettings ) ;
	SettingsLoad( PortableSettings, "Ken" ) ;
	SettingsPrint( PortableSettings ) ;
	SettingsFree( PortableSettings ) ;
	debug_log( "FIN\n\n" ) ;

}
*/
