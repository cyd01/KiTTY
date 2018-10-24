// Saved sessions enumeration.
struct enumsettings {
    HKEY key;
    int i;
	int fromFile;
	HANDLE hFile;
};

// Random seed functions enumeration
enum { DEL, OPEN_R, OPEN_W };

// PUTTY Tray / PuTTY File - global storage type
static int storagetype = 0;	// 0 = registry, 1 = file

// PUTTY Tray / PuTTY File - extra variables / structs for file
static char seedpath[2 * MAX_PATH + 10] = "\0";
static char sesspath[2 * MAX_PATH] = "\0";
static char sshkpath[2 * MAX_PATH] = "\0";
static char oldpath[2 * MAX_PATH] = "\0";
static char sessionsuffix[16] = "\0";
static char keysuffix[16] = "\0";

/* JK: structures for handling settings in memory as linked list */
struct setItem {
	char* key;
	char* value;
	struct setItem* next;
};
struct setPack {
	unsigned int fromFile;
	void* handle;
	char* fileBuf;
};

// Forward declarations for helper functions
void mungestr(const char *in, char *out);
void unmungestr(const char *in, char *out, int outlen);
static void registry_recursive_remove(HKEY key);

// Forward declarations for file functions
void *file_open_settings_w(const char *sessionname, char **errmsg);
void file_write_setting_s(void *handle, const char *key, const char *value);
void file_write_setting_i(void *handle, const char *key, int value);
void file_write_setting_filename(void *handle, const char *key, Filename value);
void file_write_setting_fontspec(void *handle, const char *key, FontSpec font);
void file_close_settings_w(void *handle);
void *file_open_settings_r(const char *sessionname);
char *file_read_setting_s(void *handle, const char *key, char *buffer, int buflen);
int file_read_setting_i(void *handle, const char *key, int defvalue);
int file_read_setting_filename(void *handle, const char *key, Filename *value);
int file_read_setting_fontspec(void *handle, const char *key, FontSpec *font);
void file_close_settings_r(void *handle);
void file_del_settings(const char *sessionname);
void *file_enum_settings_start();
char *file_enum_settings_next(void *handle, char *buffer, int buflen);
void file_enum_settings_finish(void *handle);
int file_verify_host_key(const char *hostname, int port, const char *keytype, const char *key);
void file_store_host_key(const char *hostname, int port, const char *keytype, const char *key);

// Forward declarations for registry functions
void *reg_open_settings_w(const char *sessionname, char **errmsg);
void reg_write_setting_s(void *handle, const char *key, const char *value);
void reg_write_setting_i(void *handle, const char *key, int value);
void reg_write_setting_filename(void *handle, const char *key, Filename value);
void reg_write_setting_fontspec(void *handle, const char *key, FontSpec font);
void reg_close_settings_w(void *handle);
void *reg_open_settings_r(const char *sessionname);
char *reg_read_setting_s(void *handle, const char *key, char *buffer, int buflen);
int reg_read_setting_i(void *handle, const char *key, int defvalue);
int reg_read_setting_filename(void *handle, const char *key, Filename *value);
int reg_read_setting_fontspec(void *handle, const char *key, FontSpec *font);
void reg_close_settings_r(void *handle);
void reg_del_settings(const char *sessionname);
void *reg_enum_settings_start();
char *reg_enum_settings_next(void *handle, char *buffer, int buflen);
void reg_enum_settings_finish(void *handle);
int reg_verify_host_key(const char *hostname, int port, const char *keytype, const char *key);
void reg_store_host_key(const char *hostname, int port, const char *keytype, const char *key);


/*
 * Sets storage type
 */
void set_storagetype(int new_storagetype)
{
	storagetype = new_storagetype;
}


/*
 * Write a saved session. The caller is expected to call
 * open_setting_w() to get a `void *' handle, then pass that to a
 * number of calls to write_setting_s() and write_setting_i(), and
 * then close it using close_settings_w(). At the end of this call
 * sequence the settings should have been written to the PuTTY
 * persistent storage area.
 *
 * A given key will be written at most once while saving a session.
 * Keys may be up to 255 characters long.  String values have no length
 * limit.
 * 
 * Any returned error message must be freed after use.
 *
 * STORAGETYPE SWITCHER
 */
void *open_settings_w(const char *sessionname, char **errmsg)
{
	if (storagetype == 1) {
		return file_open_settings_w(sessionname, errmsg);
	} else {
		return reg_open_settings_w(sessionname, errmsg);
	}
}


/*
 * STORAGETYPE SWITCHER
 */
void write_setting_s(void *handle, const char *key, const char *value)
{
	if (storagetype == 1) {
		file_write_setting_s(handle, key, value);
	} else {
		reg_write_setting_s(handle, key, value);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
void write_setting_i(void *handle, const char *key, int value)
{
	if (storagetype == 1) {
		file_write_setting_i(handle, key, value);
	} else {
		reg_write_setting_i(handle, key, value);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
void write_setting_filename(void *handle, const char *name, Filename result)
{
	if (storagetype == 1) {
		file_write_setting_filename(handle, name, result);
	} else {
		reg_write_setting_filename(handle, name, result);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
void write_setting_fontspec(void *handle, const char *name, FontSpec font)
{
	if (storagetype == 1) {
		file_write_setting_fontspec(handle, name, font);
	} else {
		reg_write_setting_fontspec(handle, name, font);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
void close_settings_w(void *handle)
{
	if (storagetype == 1) {
		file_close_settings_w(handle);
	} else {
		reg_close_settings_w(handle);
	}
}

/*
 * Read a saved session. The caller is expected to call
 * open_setting_r() to get a `void *' handle, then pass that to a
 * number of calls to read_setting_s() and read_setting_i(), and
 * then close it using close_settings_r().
 * 
 * read_setting_s() writes into the provided buffer and returns a
 * pointer to the same buffer.
 * 
 * If a particular string setting is not present in the session,
 * read_setting_s() can return NULL, in which case the caller
 * should invent a sensible default. If an integer setting is not
 * present, read_setting_i() returns its provided default.
 * 
 * read_setting_filename() and read_setting_fontspec() each read into
 * the provided buffer, and return zero if they failed to.
 *
 * STORAGETYPE SWITCHER
 */
void *open_settings_r(const char *sessionname)
{
	if (storagetype == 1) {
		return file_open_settings_r(sessionname);
	} else {
		return reg_open_settings_r(sessionname);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
char *read_setting_s(void *handle, const char *key, char *buffer, int buflen)
{
	if (storagetype == 1) {
		return file_read_setting_s(handle, key, buffer, buflen);
	} else {
		return reg_read_setting_s(handle, key, buffer, buflen);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
int read_setting_i(void *handle, const char *key, int defvalue)
{
	if (storagetype == 1) {
		return file_read_setting_i(handle, key, defvalue);
	} else {
		return reg_read_setting_i(handle, key, defvalue);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
int read_setting_fontspec(void *handle, const char *name, FontSpec *result)
{
	if (storagetype == 1) {
		return file_read_setting_fontspec(handle, name, result);
	} else {
		return reg_read_setting_fontspec(handle, name, result);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
int read_setting_filename(void *handle, const char *name, Filename *result)
{
	if (storagetype == 1) {
		return file_read_setting_filename(handle, name, result);
	} else {
		return reg_read_setting_filename(handle, name, result);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
void close_settings_r(void *handle)
{
	if (storagetype == 1) {
		return file_close_settings_r(handle);
	} else {
		return reg_close_settings_r(handle);
	}
}

/*
 * Delete a whole saved session.
 *
 * STORAGETYPE SWITCHER
 */
void del_settings(const char *sessionname)
{
	if (storagetype == 1) {
		file_del_settings(sessionname);
	} else {
		reg_del_settings(sessionname);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
void *enum_settings_start(int new_storagetype)
{
	storagetype = new_storagetype;

	if (storagetype == 1) {
		return file_enum_settings_start();
	} else {
		return reg_enum_settings_start();
	}
}

/*
 * STORAGETYPE SWITCHER
 */
char *enum_settings_next(void *handle, char *buffer, int buflen)
{
	if (storagetype == 1) {
		return file_enum_settings_next(handle, buffer, buflen);
	} else {
		return reg_enum_settings_next(handle, buffer, buflen);
	}
}

/*
 * STORAGETYPE SWITCHER
 */
void enum_settings_finish(void *handle)
{
	if (storagetype == 1) {
		file_enum_settings_finish(handle);
	} else {
		reg_enum_settings_finish(handle);
	}
}


/* ----------------------------------------------------------------------
 * Functions to access PuTTY's host key database.
 */

/*
 * Helper for hostkey functions (not part of storage.h)
 * NO HACK: PuttyTray / PuTTY File - This is an original function (not patched)
 */
static void hostkey_regname(char *buffer, const char *hostname, int port, const char *keytype)
{
    int len;
    strcpy(buffer, keytype);
    strcat(buffer, "@");
    len = strlen(buffer);
    len += sprintf(buffer + len, "%d:", port);
    mungestr(hostname, buffer + strlen(buffer));
}

/*
 * See if a host key matches the database entry. Return values can
 * be 0 (entry matches database), 1 (entry is absent in database),
 * or 2 (entry exists in database and is different).
 *
 * STORAGETYPE SWITCHER
 */
int verify_host_key(const char *hostname, int port, const char *keytype, const char *key)
{
	if (storagetype == 1) {
		return file_verify_host_key(hostname, port, keytype, key);
	} else {
		return reg_verify_host_key(hostname, port, keytype, key);
	}
}

/*
 * Write a host key into the database, overwriting any previous
 * entry that might have been there.
 *
 * STORAGETYPE SWITCHER
 */
/*void store_host_key(const char *hostname, int port, const char *keytype, const char *key)
{
	if (storagetype == 1) {
		file_store_host_key(hostname, port, keytype, key);
	} else {
		reg_store_host_key(hostname, port, keytype, key);
	}
}
*/

/* ----------------------------------------------------------------------
 * Functions to access PuTTY's random number seed file.
 */
/*
 * HELPER FOR RANDOM SEED FUNCTIONS (not part of storage.h)
 * Open (or delete) the random seed file.
 *
 * NO HACK: PuttyTray / PuTTY File - This is an original function (not patched)
 */
static int try_random_seed(char const *path, int action, HANDLE *ret)
{
    if (action == DEL) {
	remove(path);
	*ret = INVALID_HANDLE_VALUE;
	return FALSE;		       /* so we'll do the next ones too */
    }

    *ret = CreateFile(path,
		      action == OPEN_W ? GENERIC_WRITE : GENERIC_READ,
		      action == OPEN_W ? 0 : (FILE_SHARE_READ |
					      FILE_SHARE_WRITE),
		      NULL,
		      action == OPEN_W ? CREATE_ALWAYS : OPEN_EXISTING,
		      action == OPEN_W ? FILE_ATTRIBUTE_NORMAL : 0,
		      NULL);

    return (*ret != INVALID_HANDLE_VALUE);
}

 /*
  * HELPER FOR RANDOM SEED FUNCTIONS (not part of storage.h)
  * 
  * PARTLY HACKED: PuttyTray / PuTTY File - This is an original function (only first lines patched)
  */
static HANDLE access_random_seed(int action)
{
    HKEY rkey;
    DWORD type, size;
    HANDLE rethandle;
    char seedpath[2 * MAX_PATH + 10] = "\0";

	/* PuttyTray / PuTTY File - HACK STARTS HERE */
	if (seedpath != '\0') {
		/* JK: In PuTTY 0.58 this won't ever happen - this function was called only if (!seedpath[0])
		 * This changed in PuTTY 0.59 - read the long comment below
		 */
		return;
	}
	/* PuttyTray / PuTTY File - HACK ENDS HERE */

    /*
     * Iterate over a selection of possible random seed paths until
     * we find one that works.
     * 
     * We do this iteration separately for reading and writing,
     * meaning that we will automatically migrate random seed files
     * if a better location becomes available (by reading from the
     * best location in which we actually find one, and then
     * writing to the best location in which we can _create_ one).
     */

    /*
     * First, try the location specified by the user in the
     * Registry, if any.
     */
    size = sizeof(seedpath);
    if (RegOpenKey(HKEY_CURRENT_USER, PUTTY_REG_POS, &rkey) ==
	ERROR_SUCCESS) {
	int ret = RegQueryValueEx(rkey, "RandSeedFile",
				  0, &type, seedpath, &size);
	if (ret != ERROR_SUCCESS || type != REG_SZ)
	    seedpath[0] = '\0';
	RegCloseKey(rkey);

	if (*seedpath && try_random_seed(seedpath, action, &rethandle))
	    return rethandle;
    }

    /*
     * Next, try the user's local Application Data directory,
     * followed by their non-local one. This is found using the
     * SHGetFolderPath function, which won't be present on all
     * versions of Windows.
     */
    if (!tried_shgetfolderpath) {
	/* This is likely only to bear fruit on systems with IE5+
	 * installed, or WinMe/2K+. There is some faffing with
	 * SHFOLDER.DLL we could do to try to find an equivalent
	 * on older versions of Windows if we cared enough.
	 * However, the invocation below requires IE5+ anyway,
	 * so stuff that. */
	shell32_module = LoadLibrary("SHELL32.DLL");
	if (shell32_module) {
	    p_SHGetFolderPath = (p_SHGetFolderPath_t)
		GetProcAddress(shell32_module, "SHGetFolderPathA");
	}
    }
    if (p_SHGetFolderPath) {
	if (SUCCEEDED(p_SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA,
					NULL, SHGFP_TYPE_CURRENT, seedpath))) {
	    strcat(seedpath, "\\PUTTY.RND");
	    if (try_random_seed(seedpath, action, &rethandle))
		return rethandle;
	}

	if (SUCCEEDED(p_SHGetFolderPath(NULL, CSIDL_APPDATA,
					NULL, SHGFP_TYPE_CURRENT, seedpath))) {
	    strcat(seedpath, "\\PUTTY.RND");
	    if (try_random_seed(seedpath, action, &rethandle))
		return rethandle;
	}
    }

    /*
     * Failing that, try %HOMEDRIVE%%HOMEPATH% as a guess at the
     * user's home directory.
     */
    {
	int len, ret;

	len =
	    GetEnvironmentVariable("HOMEDRIVE", seedpath,
				   sizeof(seedpath));
	ret =
	    GetEnvironmentVariable("HOMEPATH", seedpath + len,
				   sizeof(seedpath) - len);
	if (ret != 0) {
	    strcat(seedpath, "\\PUTTY.RND");
	    if (try_random_seed(seedpath, action, &rethandle))
		return rethandle;
	}
    }

    /*
     * And finally, fall back to C:\WINDOWS.
     */
    GetWindowsDirectory(seedpath, sizeof(seedpath));
    strcat(seedpath, "\\PUTTY.RND");
    if (try_random_seed(seedpath, action, &rethandle))
	return rethandle;

    /*
     * If even that failed, give up.
     */
    return INVALID_HANDLE_VALUE;
}


/*
 * Read PuTTY's random seed file and pass its contents to a noise
 * consumer function.
 *
 * NO HACK: PuttyTray / PuTTY File - This is an original function (not patched)
 */
void read_random_seed(noise_consumer_t consumer)
{
    HANDLE seedf = access_random_seed(OPEN_R);

    if (seedf != INVALID_HANDLE_VALUE) {
	while (1) {
	    char buf[1024];
	    DWORD len;

	    if (ReadFile(seedf, buf, sizeof(buf), &len, NULL) && len)
		consumer(buf, len);
	    else
		break;
	}
	CloseHandle(seedf);
    }
}

/*
 * Write PuTTY's random seed file from a given chunk of noise.
 *
 * NO HACK: PuttyTray / PuTTY File - This is an original function (not patched)
 */
void write_random_seed(void *data, int len)
{
    HANDLE seedf = access_random_seed(OPEN_W);

    if (seedf != INVALID_HANDLE_VALUE) {
	DWORD lenwritten;

	WriteFile(seedf, data, len, &lenwritten, NULL);
	CloseHandle(seedf);
    }
}


/* ----------------------------------------------------------------------
 * Cleanup function: remove all of PuTTY's persistent state.
 *
 * NO HACK: PuttyTray / PuTTY File - This is an original function (not patched)
 */
void cleanup_all(void)
{
    HKEY key;
    int ret;
    char name[MAX_PATH + 1];

    /* ------------------------------------------------------------
     * Wipe out the random seed file, in all of its possible
     * locations.
     */
    access_random_seed(DEL);

    /* ------------------------------------------------------------
     * Destroy all registry information associated with PuTTY.
     */

    /*
     * Open the main PuTTY registry key and remove everything in it.
     */
    if (RegOpenKey(HKEY_CURRENT_USER, PUTTY_REG_POS, &key) ==
	ERROR_SUCCESS) {
	registry_recursive_remove(key);
	RegCloseKey(key);
    }
    /*
     * Now open the parent key and remove the PuTTY main key. Once
     * we've done that, see if the parent key has any other
     * children.
     */
    if (RegOpenKey(HKEY_CURRENT_USER, PUTTY_REG_PARENT,
		   &key) == ERROR_SUCCESS) {
	RegDeleteKey(key, PUTTY_REG_PARENT_CHILD);
	ret = RegEnumKey(key, 0, name, sizeof(name));
	RegCloseKey(key);
	/*
	 * If the parent key had no other children, we must delete
	 * it in its turn. That means opening the _grandparent_
	 * key.
	 */
	if (ret != ERROR_SUCCESS) {
	    if (RegOpenKey(HKEY_CURRENT_USER, PUTTY_REG_GPARENT,
			   &key) == ERROR_SUCCESS) {
		RegDeleteKey(key, PUTTY_REG_GPARENT_CHILD);
		RegCloseKey(key);
	    }
	}
    }
    /*
     * Now we're done.
     */
}


/* ----------------------------------------------------------------------
 * PUTTY FILE HELPERS (not part of storage.h)
 */
/* JK: my generic function for simplyfing error reporting */
DWORD errorShow(const char* pcErrText, const char* pcErrParam) {

	HWND hwRodic;
	DWORD erChyba;
	char pcBuf[16];
	char* pcHlaska = snewn(strlen(pcErrParam) + strlen(pcErrText) + 31, char);
	
	erChyba = GetLastError();		
	ltoa(erChyba, pcBuf, 10);

	strcpy(pcHlaska, "Error: ");
	strcat(pcHlaska, pcErrText);
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
        return 0;
    }

	sfree(pcHlaska);
	return erChyba;
};

/* JK: pack string for use as filename - pack < > : " / \ | */
static void packstr(const char *in, char *out) {
    while (*in) {
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
	createPath(dir);
	*p = '\\';
	++p;

	/* what if it already exists */
	if (!SetCurrentDirectory(dir)) {
		CreateDirectory(p, NULL);
		return SetCurrentDirectory(p);
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
	    		/* JK: failure -> revert back - but it ussualy won't work, so report error to user! *//*
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

	char *fileCont = NULL;
	DWORD fileSize;
	DWORD bytesRead;
	char *p = NULL;
	char *p2 = NULL;
	HANDLE hFile;

	char* puttypath = snewn( (MAX_PATH*2), char);

	/* JK:  save path/curdir */
	GetCurrentDirectory( (MAX_PATH*2), oldpath);

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

	/* JK: set default values - if there is a config file, it will be overwitten */
	strcpy(sesspath, puttypath);
	strcat(sesspath, "\\sessions");
	strcpy(sshkpath, puttypath);
	strcat(sshkpath, "\\sshhostkeys");
	strcpy(seedpath, puttypath);
	strcat(seedpath, "\\putty.rnd");

	hFile = CreateFile("putty.conf",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);

	/* JK: now we can pre-clean-up */
	SetCurrentDirectory(oldpath);

	if (hFile != INVALID_HANDLE_VALUE) {
		fileSize = GetFileSize(hFile, NULL);
		fileCont = snewn(fileSize+16, char);

		if (!ReadFile(hFile, fileCont, fileSize, &bytesRead, NULL)) {
			errorShow("Unable to read configuration file, falling back to defaults", NULL);
		
			/* JK: default values are already there and clean-up at end */
		}
		else {
			/* JK: parse conf file to path variables */
			*(fileCont+fileSize) = '\0';
			p = fileCont;
			while (p) {
				if (*p == ';') {	/* JK: comment -> skip line */
					p = strchr(p, '\n');
					++p;
					continue;
				}
				p2 = strchr(p, '=');
				if (!p2) break;
				*p2 = '\0';
				++p2;

				if (!strcmp(p, "sessions")) {
					p = strchr(p2, '\n');
					*p = '\0';
					joinPath(sesspath, puttypath, p2);
					p2 = sesspath+strlen(sesspath)-1;
					while ((*p2 == ' ')||(*p2 == '\n')||(*p2 == '\r')||(*p2 == '\t')) --p2;
					*(p2+1) = '\0';
				}
				else if (!strcmp(p, "sshhostkeys")) {
					p = strchr(p2, '\n');
					*p = '\0';
					joinPath(sshkpath, puttypath, p2);
					p2 = sshkpath+strlen(sshkpath)-1;
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


/* ----------------------------------------------------------------------
 * OTHER HELPERS (not part of storage.h)
 *
 * NO HACK: PuttyTray / PuTTY File - these are original functions (not patched)
 */

void mungestr(const char *in, char *out)
{
    int candot = 0;

    while (*in) {
	if (*in == ' ' || *in == '\\' || *in == '*' || *in == '?' ||
	    *in == '%' || *in < ' ' || *in > '~' || (*in == '.'
						     && !candot)) {
	    *out++ = '%';
	    *out++ = hex[((unsigned char) *in) >> 4];
	    *out++ = hex[((unsigned char) *in) & 15];
	} else
	    *out++ = *in;
	in++;
	candot = 1;
    }
    *out = '\0';
    return;
}

void unmungestr(const char *in, char *out, int outlen)
{
    while (*in) {
	if (*in == '%' && in[1] && in[2]) {
	    int i, j;

	    i = in[1] - '0';
	    i -= (i > 9 ? 7 : 0);
	    j = in[2] - '0';
	    j -= (j > 9 ? 7 : 0);

	    *out++ = (i << 4) + j;
	    if (!--outlen)
		return;
	    in += 3;
	} else {
	    *out++ = *in++;
	    if (!--outlen)
		return;
	}
    }
    *out = '\0';
    return;
}

/*
 * Recursively delete a registry key and everything under it.
 */
static void registry_recursive_remove(HKEY key)
{
    DWORD i;
    char name[MAX_PATH + 1];
    HKEY subkey;

    i = 0;
    while (RegEnumKey(key, i, name, sizeof(name)) == ERROR_SUCCESS) {
	if (RegOpenKey(key, name, &subkey) == ERROR_SUCCESS) {
	    registry_recursive_remove(subkey);
	    RegCloseKey(subkey);
	}
	RegDeleteKey(key, name);
    }
}


/* ---------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------------------------------------------------------
 * FILE FUNCTIONS
 * ---------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------------------------------------------------*/
void *file_open_settings_w(const char *sessionname, char **errmsg)
{
    char *p;
	struct setPack* sp;
    *errmsg = NULL;

	if (!sessionname || !*sessionname) {
		sessionname = "Default Settings";
	}

	/* JK: if sessionname contains [registry] -> cut it off */
	/*if ( *(sessionname+strlen(sessionname)-1) == ']') {
		p = strrchr(sessionname, '[');
		*(p-1) = '\0';
	}*/

    p = snewn(3 * strlen(sessionname) + 1, char);
    mungestr(sessionname, p);

	sp = snew( struct setPack );
	sp->fromFile = 0;
	sp->handle = NULL;

	/* JK: secure pack for filename */
	sp->fileBuf = snewn(3 * strlen(p) + 1 + 16, char);
    packstr(p, sp->fileBuf);
	strcat(sp->fileBuf, sessionsuffix);
	sfree(p);

	return sp;
}

void file_write_setting_s(void *handle, const char *key, const char *value)
{
	struct setItem *st;

	if (handle) {
		/* JK: counting max lenght of keys/values */
		((struct setPack*) handle)->fromFile = max(((struct setPack*) handle)->fromFile, strlen(key)+1);
		((struct setPack*) handle)->fromFile = max(((struct setPack*) handle)->fromFile, strlen(value)+1);

		st = ((struct setPack*) handle)->handle;
		while (st) {
			if ( strcmp(st->key, key) == 0) {
				/* this key already set -> reset */
				sfree(st->value);
				st->value = snewn( strlen(value)+1, char);
				strcpy(st->value, value);
				return;
			}
			st = st->next;
		}
		/* JK: key not found -> add to begin */
		st = snew( struct setItem );
		st->key = snewn( strlen(key)+1, char);
		strcpy(st->key, key);
		st->value = snewn( strlen(value)+1, char);
		strcpy(st->value, value);
		st->next = ((struct setPack*) handle)->handle;
		((struct setPack*) handle)->handle = st;
	}
}

void file_write_setting_i(void *handle, const char *key, int value)
{
	struct setItem *st;

	if (handle) {
		/* JK: counting max lenght of keys/values */
		((struct setPack*) handle)->fromFile = max(((struct setPack*) handle)->fromFile, strlen(key)+1);

		st = ((struct setPack*) handle)->handle;
		while (st) {
			if ( strcmp(st->key, key) == 0) {
				/* this key already set -> reset */
				sfree(st->value);
				st->value = snewn(16, char);
				itoa(value, st->value, 10);
				return;
			}
			st = st->next;
		}
		/* JK: key not found -> add to begin */
		st = snew( struct setItem );
		st->key = snewn( strlen(key)+1, char);
		strcpy(st->key, key);
		st->value = snewn(16, char);
		itoa(value, st->value, 10);
		st->next = ((struct setPack*) handle)->handle;
		((struct setPack*) handle)->handle = st;
	}
}

void file_close_settings_w(void *handle)
{
	HANDLE hFile;
	DWORD written;
	WIN32_FIND_DATA FindFile;
	char *p;
	struct setItem *st1,*st2;
	int writeok;

	if (!handle) return;

	/* JK: we will write to disk now - open file, filename stored in handle already packed */
	if ((hFile = FindFirstFile(sesspath, &FindFile)) == INVALID_HANDLE_VALUE) {
		if (!createPath(sesspath)) {
			errorShow("Unable to create directory for storing sessions", sesspath);
			return;
		}
	}
	FindClose(hFile);
	GetCurrentDirectory( (MAX_PATH*2), oldpath);
	SetCurrentDirectory(sesspath);

	hFile = CreateFile( ((struct setPack*) handle)->fileBuf, GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		errorShow("Unable to open file for writing", ((struct setPack*) handle)->fileBuf );
		return;
	}

	/* JK: allocate enough memory for all keys/values */
	p = snewn( max( 3* ((struct setPack*) handle)->fromFile ,16), char);

	/* JK: process linked list */
	st1 = ((struct setPack*) handle)->handle;
	writeok = 1;

	while (st1) {
		mungestr(st1->key, p);
		writeok = writeok && WriteFile( (HANDLE) hFile, p, strlen(p), &written, NULL);
		writeok = writeok && WriteFile( (HANDLE) hFile, "\\", 1, &written, NULL);

		mungestr(st1->value, p);
		writeok = writeok && WriteFile( (HANDLE) hFile, p, strlen(p), &written, NULL);
		writeok = writeok && WriteFile( (HANDLE) hFile, "\\\n", 2, &written, NULL);

		if (!writeok) {
			errorShow("Unable to save settings", st1->key);
			return;
			/* JK: memory should be freed here - fixme */
		}

		st2 = st1->next;
		sfree(st1->key);
		sfree(st1->value);
		sfree(st1);
		st1 = st2;
	}

	sfree(((struct setPack*) handle)->fileBuf);
	CloseHandle( (HANDLE)hFile );
	SetCurrentDirectory(oldpath);
}

void *file_open_settings_r(const char *sessionname)
{
    HKEY subkey1, sesskey;
    char *p;
	char *ses;
	char *fileCont;
	DWORD fileSize;
	DWORD bytesRead;
	HANDLE hFile;
	struct setPack* sp;
	struct setItem *st1, *st2;

	sp = snew( struct setPack );

	if (!sessionname || !*sessionname) {
		sessionname = "Default Settings";
	}

	/* JK: in the first call of this function we initialize path variables */
	if (*sesspath == '\0') {
		loadPath();
	}

	/* JK: if sessionname contains [registry] -> cut it off in another buffer */
	/*if ( *(sessionname+strlen(sessionname)-1) == ']') {
		ses = snewn(strlen(sessionname)+1, char);
		strcpy(ses, sessionname);

		p = strrchr(ses, '[');
		*(p-1) = '\0';

		p = snewn(3 * strlen(ses) + 1, char);
		mungestr(ses, p);
		sfree(ses);

		sp->fromFile = 0;
	}
	else {*/
		p = snewn(3 * strlen(sessionname) + 1 + 16, char);
		mungestr(sessionname, p);
		strcat(p, sessionsuffix);

		sp->fromFile = 1;
	//}

	/* JK: default settings must be read from registry */
	/* 8.1.2007 - 0.1.6 try to load them from file if exists - nasty code duplication */
	if (!strcmp(sessionname, "Default Settings")) {
		GetCurrentDirectory( (MAX_PATH*2), oldpath);
		if (SetCurrentDirectory(sesspath)) {
			hFile = CreateFile(p, GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		}
		else {
			hFile = INVALID_HANDLE_VALUE;
		}
		SetCurrentDirectory(oldpath);
		
		if (hFile == INVALID_HANDLE_VALUE) {
			sp->fromFile = 0;
		}
		else {
			sp->fromFile = 1;
			CloseHandle(hFile);
		}
	}

	if (sp->fromFile) {
		/* JK: session is in file -> open dir/file */
		GetCurrentDirectory( (MAX_PATH*2), oldpath);
		if (SetCurrentDirectory(sesspath)) {
			hFile = CreateFile(p, GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		}
		else {
			hFile = INVALID_HANDLE_VALUE;
		}
		SetCurrentDirectory(oldpath);
		
		if (hFile == INVALID_HANDLE_VALUE) {
			/* JK: some error occured -> just report and fail */

			/* JK: PSCP/PLINK always try to load settings for sessionname=hostname (to what PSCP/PLINK is just connecting)
			   These settings usually doesn't exist.
			   So for PSCP/PLINK, do not report error - so when compiling PSCP/PLINK, comment line below
			   (errorShow("Unable to load file for reading", p);)
			*/
//#error read comment above
			errorShow("Unable to load file for reading", p);

			sfree(p);
			return NULL;
		}

		/* JK: succes -> load structure setPack from file */
		fileSize = GetFileSize(hFile, NULL);
		fileCont = snewn(fileSize+16, char);

		if (!ReadFile(hFile, fileCont, fileSize, &bytesRead, NULL)) {
			errorShow("Unable to read session from file", p);
			sfree(p);
			return NULL;
		}
		sfree(p);

		st1 = snew( struct setItem );
		sp->fromFile = 1;
		sp->handle = st1;
		
		p = fileCont;
		sp->fileBuf = fileCont; /* JK: remeber for memory freeing */

		/* pJK: arse file in format:
		 * key1\value1\
		 * ...
		*/
		while (p < (fileCont+fileSize)) {
			st1->key = p;
			p = strchr(p, '\\');
			if (!p) break;
			*p = '\0';
			++p;
			st1->value = p;
			p = strchr(p, '\\');
			if (!p) break;
			*p = '\0';
			++p;
			++p; /* for "\\\n" - human readable files */

			st2 = snew( struct setItem );
			st2->next = NULL;
			st2->key = NULL;
			st2->value = NULL;

			st1->next = st2;
			st1 = st2;
		}
		CloseHandle(hFile);
	}
	else {
		/* JK: session is in registry */
		if (RegOpenKey(HKEY_CURRENT_USER, puttystr, &subkey1) != ERROR_SUCCESS) {
			sesskey = NULL;
		}
		else {
			if (RegOpenKey(subkey1, p, &sesskey) != ERROR_SUCCESS) {
				sesskey = NULL;
			}
			RegCloseKey(subkey1);
		}
		sp->fromFile = 0;
		sp->handle = sesskey;
		sfree(p);
	}

	return sp;
}

char *file_read_setting_s(void *handle, const char *key, char *buffer, int buflen)
{
    DWORD type;
	struct setItem *st;
	char *p;
	DWORD size = buflen;

	if (!handle) return NULL;	/* JK: new in 0.1.3 */

	if (((struct setPack*) handle)->fromFile) {
		
		p = snewn(3 * strlen(key) + 1, char);
		mungestr(key, p);

		st = ((struct setPack*) handle)->handle;
		while (st->key) {
			if ( strcmp(st->key, p) == 0) {
				unmungestr(st->value, buffer, buflen);
				return st->value;				
			}
			st = st->next;
		}
	}
	else {
		handle = ((struct setPack*) handle)->handle;

		if (!handle || RegQueryValueEx((HKEY) handle, key, 0, &type, buffer, &size) != ERROR_SUCCESS ||	type != REG_SZ) {
			return NULL;
		}
		else {
			return buffer;
		}
	}
	/* JK: should not end here -> value not found in file */
	return NULL;
}

int file_read_setting_i(void *handle, const char *key, int defvalue)
{
    DWORD type, val, size;
	struct setItem *st;
    size = sizeof(val);

	if (!handle) return 0;	/* JK: new in 0.1.3 */

	if (((struct setPack*) handle)->fromFile) {
		st = ((struct setPack*) handle)->handle;
		while (st->key) {
			if ( strcmp(st->key, key) == 0) {
				return atoi(st->value);				
			}
			st = st->next;
		}
	}
	else {
		handle = ((struct setPack*) handle)->handle;

		if (!handle || RegQueryValueEx((HKEY) handle, key, 0, &type, (BYTE *) &val, &size) != ERROR_SUCCESS || size != sizeof(val) || type != REG_DWORD) {
			return defvalue;
		}
		else {
			return val;
		}
	}
	/* JK: should not end here -> value not found in file */
	return defvalue;
}

int file_read_setting_fontspec(void *handle, const char *name, FontSpec *result)
{
    char *settingname;
    FontSpec ret;

    if (!file_read_setting_s(handle, name, ret.name, sizeof(ret.name)))
	return 0;
    settingname = dupcat(name, "IsBold", NULL);
    ret.isbold = file_read_setting_i(handle, settingname, -1);
    sfree(settingname);
    if (ret.isbold == -1) return 0;
    settingname = dupcat(name, "CharSet", NULL);
    ret.charset = file_read_setting_i(handle, settingname, -1);
    sfree(settingname);
    if (ret.charset == -1) return 0;
    settingname = dupcat(name, "Height", NULL);
    ret.height = file_read_setting_i(handle, settingname, INT_MIN);
    sfree(settingname);
    if (ret.height == INT_MIN) return 0;
    *result = ret;
    return 1;
}

void file_write_setting_fontspec(void *handle, const char *name, FontSpec font)
{
    char *settingname;

    file_write_setting_s(handle, name, font.name);
    settingname = dupcat(name, "IsBold", NULL);
    file_write_setting_i(handle, settingname, font.isbold);
    sfree(settingname);
    settingname = dupcat(name, "CharSet", NULL);
    file_write_setting_i(handle, settingname, font.charset);
    sfree(settingname);
    settingname = dupcat(name, "Height", NULL);
    file_write_setting_i(handle, settingname, font.height);
    sfree(settingname);
}

int file_read_setting_filename(void *handle, const char *name, Filename *result)
{
    return !!file_read_setting_s(handle, name, result->path, sizeof(result->path));
}

void file_write_setting_filename(void *handle, const char *name, Filename result)
{
    file_write_setting_s(handle, name, result.path);
}

void file_close_settings_r(void *handle)
{
	if (!handle) return;	/* JK: new in 0.1.3 */

	if (((struct setPack*) handle)->fromFile) {
		struct setItem *st1, *st2;

		st1 = ((struct setPack*) handle)->handle;
		while (st1) {
			st2 = st1->next;
			sfree(st1);
			st1 = st2;
		}
		sfree( ((struct setPack*) handle)->fileBuf );
		sfree(handle);
	}
	else {
		handle = ((struct setPack*) handle)->handle;
	    RegCloseKey((HKEY) handle);
	}
}

void file_del_settings(const char *sessionname)
{
    HKEY subkey1;
    char *p;
	char *p2;

	/* JK: if sessionname contains [registry] -> cut it off and delete from registry */
	/*if ( *(sessionname+strlen(sessionname)-1) == ']') {

		p = strrchr(sessionname, '[');
		*(p-1) = '\0';

		p = snewn(3 * strlen(sessionname) + 1, char);
		mungestr(sessionname, p);
		
		if (RegOpenKey(HKEY_CURRENT_USER, puttystr, &subkey1) != ERROR_SUCCESS)	return;

		RegDeleteKey(subkey1, p);
		RegCloseKey(subkey1);
	}
	else {*/
		/* JK: delete from file - file itself */

		p = snewn(3 * strlen(sessionname) + 1, char);
		mungestr(sessionname, p);
		p2 = snewn(3 * strlen(p) + 1, char);
		packstr(p, p2);

		GetCurrentDirectory( (MAX_PATH*2), oldpath);
		if (SetCurrentDirectory(sesspath)) {
			if (!DeleteFile(p2))
			{
				errorShow("Unable to delete settings.", NULL);
			}
			SetCurrentDirectory(oldpath);
		}
	//}

	sfree(p);
}

void *file_enum_settings_start(void)
{
    struct enumsettings *ret;
    HKEY key;

	/* JK: in the first call of this function we can initialize path variables */
	if (*sesspath == '\0') {
		loadPath();
	}
	/* JK: we have path variables */
	
	/* JK: let's do what this function should normally do */
	ret = snew(struct enumsettings);

	if (RegOpenKey(HKEY_CURRENT_USER, puttystr, &key) != ERROR_SUCCESS) {
		/*
		 * JK: nothing in registry -> pretend we found it, first call to file_enum_settings_next
		 * will solve this by starting scanning dir sesspath
		*/
	}
	ret->key = key;
	ret->fromFile = 0;
	ret->hFile = NULL;
	ret->i = 0;

    return ret;
}

char *file_enum_settings_next(void *handle, char *buffer, int buflen)
{
	struct enumsettings *e = (struct enumsettings *) handle;
    WIN32_FIND_DATA FindFileData;
	HANDLE hFile;
	char *otherbuf;
	
	if (!handle) return NULL;	/* JK: new in 0.1.3 */
	
	otherbuf = snewn( (3*buflen)+1, char); /* must be here */

	if (! ((struct enumsettings *)handle)->fromFile ) {

	    /*if (RegEnumKey(e->key, e->i++, otherbuf, 3 * buflen) == ERROR_SUCCESS) {
			unmungestr(otherbuf, buffer, buflen);
			strcat(buffer, " [registry]");
			sfree(otherbuf);
			return buffer;
		}
		else {*/
			/* JK: registry scanning done, starting scanning directory "sessions" */
			((struct enumsettings *)handle)->fromFile = 1;
			GetCurrentDirectory( (MAX_PATH*2), oldpath);
			if (!SetCurrentDirectory(sesspath)) {
				sfree(otherbuf);
				return NULL;
			}
			hFile = FindFirstFile("*", &FindFileData);

			/* JK: skip directories (extra check for "." and ".." too, seems to bug on some machines) */
			while ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || FindFileData.cFileName[0] == '.') { // HACK: PUTTY TRAY / PUTTY FILE: Fixed directory check
				if (!FindNextFile(hFile,&FindFileData)) {
					sfree(otherbuf);
					return NULL;
				}
			}
			/* JK: a file found */
			if (hFile != INVALID_HANDLE_VALUE && !((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || FindFileData.cFileName[0] == '.')) { // HACK: PUTTY TRAY / PUTTY FILE: Fixed directory check
				((struct enumsettings *)handle)->hFile = hFile;
				unmungestr(FindFileData.cFileName, buffer, buflen);
				sfree(otherbuf);
				/* JK: cut off sessionsuffix */
				otherbuf = buffer + strlen(buffer) - strlen(sessionsuffix);
				if (strncmp(otherbuf, sessionsuffix, strlen(sessionsuffix)) == 0) {
					*otherbuf = '\0';
				}
				return buffer;
			}
			else {
				/* JK: not a single file found -> give up */
				sfree(otherbuf);
				return NULL;
			}
		//}
	}
	else if ( ((struct enumsettings *)handle)->fromFile ) {
		if (FindNextFile(((struct enumsettings *)handle)->hFile,&FindFileData) && !((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || FindFileData.cFileName[0] == '.')) { // HACK: PUTTY TRAY / PUTTY FILE: Fixed directory check
			unmungestr(FindFileData.cFileName, buffer, buflen);
			sfree(otherbuf);
			/* JK: cut off sessionsuffix */
			otherbuf = buffer + strlen(buffer) - strlen(sessionsuffix);
			if (strncmp(otherbuf, sessionsuffix, strlen(sessionsuffix)) == 0) {
				*otherbuf = '\0';
			}
			return buffer;
		}
		else {
			sfree(otherbuf);
			return NULL;
		}
	}
	/* JK: should not end here */
	sfree(otherbuf);
	return NULL;
}

void file_enum_settings_finish(void *handle)
{
    struct enumsettings *e = (struct enumsettings *) handle;
	if (!handle) return;	/* JK: new in 0.1.3 */

    RegCloseKey(e->key);
	if (((struct enumsettings *)handle)->hFile != NULL) { FindClose(((struct enumsettings *)handle)->hFile); }
	SetCurrentDirectory(oldpath);
	sfree(e);
}

int file_verify_host_key(const char *hostname, int port,
		    const char *keytype, const char *key)
{
    char *otherstr, *regname;
    int len;
	HKEY rkey;
    DWORD readlen;
    DWORD type;
    int ret, compare, userMB;

	DWORD fileSize;
	DWORD bytesRW;
	char *p;
	HANDLE hFile;
	WIN32_FIND_DATA FindFile;

    len = 1 + strlen(key);

    /* Now read a saved key in from the registry and see what it says. */
    otherstr = snewn(len, char);
    regname = snewn(3 * (strlen(hostname) + strlen(keytype)) + 15, char);

    hostkey_regname(regname, hostname, port, keytype);

	/* JK: settings on disk - every hostkey as file in dir */
	GetCurrentDirectory( (MAX_PATH*2), oldpath);
	if (SetCurrentDirectory(sshkpath)) {
		
		p = snewn(3 * strlen(regname) + 1 + 16, char);
		packstr(regname, p);
		strcat(p, keysuffix);

		hFile = CreateFile(p, GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		SetCurrentDirectory(oldpath);

		if (hFile != INVALID_HANDLE_VALUE) {
			/* JK: ok we got it -> read it to otherstr */
			fileSize = GetFileSize(hFile, NULL);
			otherstr = snewn(fileSize+1, char);
			ReadFile(hFile, otherstr, fileSize, &bytesRW, NULL);
			*(otherstr+fileSize) = '\0';

			compare = strcmp(otherstr, key);

			CloseHandle(hFile);
			sfree(otherstr);
			sfree(regname);
			sfree(p);

			if (compare) { /* key is here, but different */
				return 2;
			}
			else { /* key is here and match */
				return 0;
			}
		}
		else {
			/* not found as file -> try registry */
			sfree(p);
		}
	}
	else {
		/* JK: there are no hostkeys as files -> try registry -> nothing to do here now */
	}
	
	/* JK: directory/file not found -> try registry */
	if (RegOpenKey(HKEY_CURRENT_USER, PUTTY_REG_POS "\\SshHostKeys", &rkey) != ERROR_SUCCESS) {
		return 1;		       /* key does not exist in registry */
	}

    readlen = len;
    ret = RegQueryValueEx(rkey, regname, NULL, &type, otherstr, &readlen);

    if (ret != ERROR_SUCCESS && ret != ERROR_MORE_DATA &&
	!strcmp(keytype, "rsa")) {
	/*
	 * Key didn't exist. If the key type is RSA, we'll try
	 * another trick, which is to look up the _old_ key format
	 * under just the hostname and translate that.
	 */
	char *justhost = regname + 1 + strcspn(regname, ":");
	char *oldstyle = snewn(len + 10, char);	/* safety margin */
	readlen = len;
	ret = RegQueryValueEx(rkey, justhost, NULL, &type,
			      oldstyle, &readlen);

	if (ret == ERROR_SUCCESS && type == REG_SZ) {
	    /*
	     * The old format is two old-style bignums separated by
	     * a slash. An old-style bignum is made of groups of
	     * four hex digits: digits are ordered in sensible
	     * (most to least significant) order within each group,
	     * but groups are ordered in silly (least to most)
	     * order within the bignum. The new format is two
	     * ordinary C-format hex numbers (0xABCDEFG...XYZ, with
	     * A nonzero except in the special case 0x0, which
	     * doesn't appear anyway in RSA keys) separated by a
	     * comma. All hex digits are lowercase in both formats.
	     */
	    char *p = otherstr;
	    char *q = oldstyle;
	    int i, j;

	    for (i = 0; i < 2; i++) {
		int ndigits, nwords;
		*p++ = '0';
		*p++ = 'x';
		ndigits = strcspn(q, "/");	/* find / or end of string */
		nwords = ndigits / 4;
		/* now trim ndigits to remove leading zeros */
		while (q[(ndigits - 1) ^ 3] == '0' && ndigits > 1)
		    ndigits--;
		/* now move digits over to new string */
		for (j = 0; j < ndigits; j++)
		    p[ndigits - 1 - j] = q[j ^ 3];
		p += ndigits;
		q += nwords * 4;
		if (*q) {
		    q++;	       /* eat the slash */
		    *p++ = ',';	       /* add a comma */
		}
		*p = '\0';	       /* terminate the string */
	    }

	    /*
	     * Now _if_ this key matches, we'll enter it in the new
	     * format. If not, we'll assume something odd went
	     * wrong, and hyper-cautiously do nothing.
	     */
	    if (!strcmp(otherstr, key))
		RegSetValueEx(rkey, regname, 0, REG_SZ, otherstr,
			      strlen(otherstr) + 1);
		/* JK: session is not saved to file - fixme */
	}
    }

    compare = strcmp(otherstr, key);

	if (ret == ERROR_MORE_DATA || (ret == ERROR_SUCCESS && type == REG_SZ && compare)) {
		RegCloseKey(rkey);
		return 2;		       /* key is different in registry */
	}
	else if (ret != ERROR_SUCCESS || type != REG_SZ) {
		RegCloseKey(rkey);
		return 1;		       /* key does not exist in registry */
	}
	else { /* key matched OK in registry */
		/* JK: matching key found in registry -> warn user, ask what to do */
		p = snewn(256, char);
		userMB = MessageBox(NULL, "The host key is cached in the Windows registry. "
			"Do you want to move it to a file? \n\n"
			"Yes \t-> Move to file (and delete from registry)\n"
			"No \t-> Copy to file (and keep in registry)\n"
			"Cancel \t-> nothing will be done\n", "Security risk", MB_YESNOCANCEL|MB_ICONWARNING);

		if ((userMB == IDYES) || (userMB == IDNO)) {
			/* JK: save key to file */
			if ((hFile = FindFirstFile(sshkpath, &FindFile)) == INVALID_HANDLE_VALUE) {
				createPath(sshkpath);
			}
			FindClose(hFile);
			SetCurrentDirectory(sshkpath);

			p = snewn(3*strlen(regname) + 1 + 16, char);
			packstr(regname, p);
			strcat(p, keysuffix);
			
			hFile = CreateFile(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

			if (hFile == INVALID_HANDLE_VALUE) {
				errorShow("Unable to create file (key won't be deleted from registry)", p);
				userMB = IDNO;
			}
			else {
				if (!WriteFile(hFile, key, strlen(key), &bytesRW, NULL)) {
					errorShow("Unable to save key to file (key won't be deleted from registry)", NULL);
					userMB = IDNO;
				}
				CloseHandle(hFile);
			}
		}
		if (userMB == IDYES) {
			/* delete from registry */
			if (RegDeleteValue(rkey, regname) != ERROR_SUCCESS) {
				errorShow("Unable to delete registry value", regname);
			}
		}
		/* JK: else (Cancel) -> nothing to be done right now */
		
		RegCloseKey(rkey);

		sfree(otherstr);
		sfree(regname);
		return 0;		       
	}
}

void file_store_host_key(const char *hostname, int port,
		    const char *keytype, const char *key)
{
    char *regname = NULL;
	WIN32_FIND_DATA FindFile;
    HANDLE hFile = NULL;
	char* p = NULL;
	DWORD bytesWritten;

    regname = snewn(3 * (strlen(hostname) + strlen(keytype)) + 15, char);
    hostkey_regname(regname, hostname, port, keytype);

	/* JK: save hostkey to file in dir */
	if ((hFile = FindFirstFile(sshkpath, &FindFile)) == INVALID_HANDLE_VALUE) {
		createPath(sshkpath);
	}
	FindClose(hFile);
	GetCurrentDirectory( (MAX_PATH*2), oldpath);
	SetCurrentDirectory(sshkpath);

	p = snewn(3*strlen(regname) + 1, char);
	packstr(regname, p);
	strcat(p, keysuffix);
	hFile = CreateFile(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		errorShow("Unable to create file", p);
	}
	else {
		if (!WriteFile(hFile, key, strlen(key), &bytesWritten, NULL)) {
			errorShow("Unable to save key to file", NULL);
		}
		CloseHandle(hFile);
	}
	SetCurrentDirectory(oldpath);

    sfree(p);
	sfree(regname);
}


/* ---------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------------------------------------------------------
 * REGISTRY FUNCTIONS
 * ---------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------------------------------------------------*/
void *reg_open_settings_w(const char *sessionname, char **errmsg)
{
    HKEY subkey1, sesskey;
    int ret;
      return (void *) sesskey;
  }
  
void reg_write_setting_s(void *handle, const char *key, const char *value)
  {
      if (handle)
  	RegSetValueEx((HKEY) handle, key, 0, REG_SZ, value,
  		      1 + strlen(value));
  }
  
void reg_write_setting_i(void *handle, const char *key, int value)
  {
      if (handle)
  	RegSetValueEx((HKEY) handle, key, 0, REG_DWORD,
  		      (CONST BYTE *) &value, sizeof(value));
  }
  
void reg_close_settings_w(void *handle)
  {
      RegCloseKey((HKEY) handle);
  }
  
void *reg_open_settings_r(const char *sessionname)
  {
      HKEY subkey1, sesskey;
      char *p;
      return (void *) sesskey;
  }
  
char *reg_read_setting_s(void *handle, const char *key, char *buffer, int buflen)
  {
      DWORD type, size;
      size = buflen;
  	return buffer;
  }
  
int reg_read_setting_i(void *handle, const char *key, int defvalue)
  {
      DWORD type, val, size;
      size = sizeof(val);
  	return val;
  }

int reg_read_setting_fontspec(void *handle, const char *name, FontSpec *result)
{
    char *settingname;
    FontSpec ret;
  
    if (!reg_read_setting_s(handle, name, ret.name, sizeof(ret.name)))
	return 0;
    settingname = dupcat(name, "IsBold", NULL);
    ret.isbold = reg_read_setting_i(handle, settingname, -1);
    sfree(settingname);
    if (ret.isbold == -1) return 0;
    settingname = dupcat(name, "CharSet", NULL);
    ret.charset = reg_read_setting_i(handle, settingname, -1);
    sfree(settingname);
    if (ret.charset == -1) return 0;
    settingname = dupcat(name, "Height", NULL);
    ret.height = reg_read_setting_i(handle, settingname, INT_MIN);
    sfree(settingname);
    if (ret.height == INT_MIN) return 0;
    *result = ret;
    return 1;
}

void reg_write_setting_fontspec(void *handle, const char *name, FontSpec font)
{
    char *settingname;

    reg_write_setting_s(handle, name, font.name);
    settingname = dupcat(name, "IsBold", NULL);
    reg_write_setting_i(handle, settingname, font.isbold);
    sfree(settingname);
    settingname = dupcat(name, "CharSet", NULL);
    reg_write_setting_i(handle, settingname, font.charset);
    sfree(settingname);
    settingname = dupcat(name, "Height", NULL);
    reg_write_setting_i(handle, settingname, font.height);
    sfree(settingname);
}
  
int reg_read_setting_filename(void *handle, const char *name, Filename *result)
{
    return !!reg_read_setting_s(handle, name, result->path, sizeof(result->path));
}

void reg_write_setting_filename(void *handle, const char *name, Filename result)
{
    reg_write_setting_s(handle, name, result.path);
}
  
void reg_close_settings_r(void *handle)
{
    RegCloseKey((HKEY) handle);
}
  
void reg_del_settings(const char *sessionname)
{
    HKEY subkey1;
    char *p;
    RegCloseKey(subkey1);
}

/*
struct enumsettings {
    HKEY key;
    int i;
};
*/
void *reg_enum_settings_start(int storagetype)
{
    struct enumsettings *ret;
    HKEY key;
    return ret;
}
  
char *reg_enum_settings_next(void *handle, char *buffer, int buflen)
{
    struct enumsettings *e = (struct enumsettings *) handle;
    char *otherbuf;
}
  
void reg_enum_settings_finish(void *handle)
{
    struct enumsettings *e = (struct enumsettings *) handle;
    RegCloseKey(e->key);
    sfree(e);
}
  
int reg_verify_host_key(const char *hostname, int port,
		    const char *keytype, const char *key)
{
    char *otherstr, *regname;
	return 0;		       /* key matched OK in registry */
}

void reg_store_host_key(const char *hostname, int port,
  		    const char *keytype, const char *key)
{
    char *regname;
	return 0;		       /* key matched OK in registry */
}

void store_host_key(const char *hostname, int port,
		    const char *keytype, const char *key)
{
    char *regname;
    HKEY rkey;

    regname = snewn(3 * (strlen(hostname) + strlen(keytype)) + 15, char);

    hostkey_regname(regname, hostname, port, keytype);

    if (RegCreateKey(HKEY_CURRENT_USER, PUTTY_REG_POS "\\SshHostKeys",
		     &rkey) == ERROR_SUCCESS) {
	RegSetValueEx(rkey, regname, 0, REG_SZ, key, strlen(key) + 1);
	RegCloseKey(rkey);

      } /* else key does not exist in registry */
  
      sfree(regname);
 }
