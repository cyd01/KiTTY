#ifndef SAVEMODE_REG
#define SAVEMODE_REG 0
#endif
#ifndef SAVEMODE_FILE
#define SAVEMODE_FILE 1
#endif
#ifndef SAVEMODE_DIR
#define SAVEMODE_DIR 2
#endif

#define EMERGENCY_INIT int emergency_brake_count = 200000, while_iterations=0 ;
#define EMERGENCY_BREAK if( while_iterations++ > emergency_brake_count ) { break ; }

#include "kitty_commun.h"
#include "kitty_tools.h"

int get_param( const char * val ) ;
void mungestr(const char *in, char *out);
void unmungestr( const char *in, char *out, int outlen ) ;
void packstr(const char *in, char *out) ;
DWORD errorShow(const char* pcErrText, const char* pcErrParam) ;
char *itoa(int value, char *string, int radix);
int createPath(char* dir) ;
char *dupstr(const char *s);

extern char sesspath[2 * MAX_PATH] ;
extern char oldpath[2 * MAX_PATH] ;
extern char initialsesspath[2 * MAX_PATH] ;
extern char sessionsuffix[16] ;
extern char sshkpath[2 * MAX_PATH] ;
extern char keysuffix[16] ;
extern char jumplistpath[2 * MAX_PATH] ;

struct SettingsItem {
	char * name ;
	char * value ;
	struct SettingsItem * pNext ; 
	struct SettingsItem * pPrevious ; 
} ;
typedef struct SettingsItem SettingsItem, *HSettingsItem ;

struct SettingsList {
	char * filename ;
	int num ;
	HSettingsItem first ;
	HSettingsItem last ;
} ;
typedef struct SettingsList SettingsList, *HSettingsList;

extern HSettingsList PortableSettings ;

HSettingsItem SettingsNewItem( const char * name, const char * value ) ;
void SettingsFreeItem( HSettingsItem item ) ;

HSettingsList SettingsInit() ;
void SettingsDelItem( HSettingsList list, const char * key ) ;
void SettingsAddItem( HSettingsList list, const char * name, const char * value ) ;
void SettingsFree( HSettingsList list ) ;
char * SettingsKey( HSettingsList list, const char * key ) ;
char * SettingsKey_str( HSettingsList list, const char * key ) ;
int SettingsKey_int( HSettingsList list, const char * key, const int defvalue ) ;

void SettingsLoad( HSettingsList list, const char * filename ) ;
void SettingsSave( HSettingsList list, const char * filename ) ;

int loadPath() ;
char * SetInitialSessPath( void ) ;
char * GetSessPath( void ) ;
bool SessPathIsInitial( void ) ;
bool IsThereDefaultSessionFile( void ) ;

bool ReadPortableValue(const char *buffer, const char * name, char * value, const int maxlen) ;
