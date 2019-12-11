#include "mini.h"

#ifdef DEBUG_MODE
#include "leaktracker.h"
#endif

#define STYPE_NULL 0
#define STYPE_SECTION 1
#define STYPE_KEY 2

#define SINI_MAX_SIZE 4096


SSECTION * newSECTION( const char * name ) {
	SSECTION * newSection = NULL ;
	if( name==NULL ) return NULL ;
	if( strlen(name)<=0 ) return NULL ;
	if( ( newSection = (SSECTION*) malloc( sizeof( SSECTION ) ) ) != NULL ) {
		newSection->type = STYPE_SECTION ;
		if( ( newSection->name = (char*) malloc( strlen( name ) + 1 ) ) != NULL )
			strcpy( newSection->name, name ) ;
		newSection->next = NULL ;
		newSection->first = NULL ;
		}
	return newSection ;
	}

SKEY * newKEY( const char * name, const char * value ) {
	SKEY * newKey = NULL ;
	if( name==NULL ) return NULL ;
	if( strlen(name)<= 0 ) return NULL ;
	if( ( newKey = (SKEY*) malloc( sizeof( SKEY ) ) ) != NULL ) {
		newKey->type = STYPE_KEY ;
		if( ( newKey->name = (char*) malloc( strlen( name ) + 1 ) ) != NULL )
			strcpy( newKey->name, name ) ;
		if( ( newKey->value = (char*) malloc( strlen( value ) + 1 ) ) != NULL )
			strcpy( (char*)newKey->value, value ) ;
		newKey->next = NULL ;
		}
	return newKey ;
	}

SINI * newINI( void ) {
	SINI * newIni = NULL ;
	if( ( newIni = (SINI*) malloc( sizeof( SINI ) ) ) != NULL ) {
		newIni->name = NULL ;
		newIni->first = NULL ;
		}
	return newIni ;
	}

void freeSECTION( SSECTION ** Section ) {
	if( Section == NULL ) return ;
	if( (*Section) == NULL ) return ;

	if( (*Section)->next != NULL ) 
		{ freeSECTION( &((*Section)->next) ) ; (*Section)->next = NULL ; }
	if( (*Section)->first != NULL ) 
		{ freeKEY( &((*Section)->first) ) ; (*Section)->first = NULL ; }

	if( (*Section)->name != NULL ) { free( (*Section)->name ) ; (*Section)->name = NULL ; }

	(*Section)->type = STYPE_NULL ;

	free( *Section ) ;
	(*Section) = NULL ;
	}

void freeKEY( SKEY ** Key ) {
	if( Key == NULL ) return ;
	if( (*Key) == NULL ) return ;

	if( (*Key)->next != NULL ) 
		{ freeKEY( &((*Key)->next) ) ; (*Key)->next = NULL ; }
	
	if( (*Key)->name != NULL ) { free( (*Key)->name ) ; (*Key)->name = NULL ; }
	if( (*Key)->value != NULL ) { free( (*Key)->value ) ; (*Key)->value = NULL ; }
	
	(*Key)->type = STYPE_NULL ;
	
	free( *Key ) ;
	(*Key) = NULL ;
	}
	
void freeINI( SINI ** Ini ) {
	if( Ini == NULL ) return ;
	if( (*Ini) == NULL ) return ;
	if( (*Ini)->name != NULL ) { free( (*Ini)->name ) ; (*Ini)->name = NULL ; }
	freeSECTION( & ((*Ini)->first) ) ;
	free( *Ini ) ;
	(*Ini) = NULL ;
	}

int addSECTION( SSECTION * Section, SSECTION * SectionAdd ) {
	SSECTION * Current ;
	if( Section == NULL ) return 0 ;
	if( SectionAdd == NULL ) return 0 ;
	Current = Section->next ;
	
	if( Current == NULL ) {
		Section->next = SectionAdd ;
		}
	else {
		while( Current->next != NULL ) { Current = Current->next ; }
		Current->next = SectionAdd ;
		}
	return 1 ;
	}
	
int addINI( SINI * Ini, SSECTION * Section ) {
	int return_code = 1 ;
	if( Ini->first == NULL ) Ini->first = Section ;
	else return_code = addSECTION( Ini->first, Section ) ;
	return return_code ;
	}

int delSECTION( SSECTION * Section, const char * name ) {
	int return_code = 0 ;
	SSECTION * Current, * Last = NULL, * Next = NULL ;
	if( Section == NULL ) return 0 ;
	if( name == NULL ) return 0 ;
	if( strlen( name ) == 0 ) return 0 ; 
	Current = Section->next ;
	if( Current == NULL ) return 0 ;

	while( (Current != NULL) && ( strcmp( Current->name, name ) ) ) {
		Last = Current ;
		Current = Current->next ;
		Next = Current->next ;
		}
	if( !strcmp( Current->name, name ) && ( Current->type = STYPE_SECTION ) ) {
		freeSECTION( &Current ) ;
		if( Last != NULL ) Last->next = Next ;
		else Section->next = Next ;
		return_code = 1 ;
		}
	return return_code ;
	}

int addKEY( SSECTION * Section, SKEY * Key ) {
	SKEY * Current ;
	if( Section == NULL ) return 0 ;
	if( Key == NULL ) return 0 ;
	
	if( ( Current = getKEY( Section, Key->name ) ) != NULL ) { // Si la clé existe déjà on la supprime d'abord
		//if( !delKEY( Section, Key->name ) ) return 0 ;
		if( Current->value != NULL ) { free( Current->value ) ; Current->value = NULL ; }
		if( ( Current->value = malloc( strlen( (const char*)(Key->value) ) + 1 ) ) == NULL ) return 0 ;
		//strcpy( Current->value, Key->value ) ;
		memcpy( Current->value, Key->value, strlen( (const char*)Key->value ) + 1 ) ;
		freeKEY( &Key ) ;
		return 1 ; 
		}
	
	Current = Section->first ;

	if( Current == NULL ) { 
		Section->first = Key ;
		}
	else {
		while( Current->next != NULL ) { Current = Current->next ; }
		Current->next = Key ;
		}
	return 1 ;
	}

void freeSKEY( SKEY ** c ) { free( *c ) ; }
	
int delKEY( SSECTION * Section, const char * name ) {
	int return_code = 0 ;
	SKEY * Current, * Last = NULL, * Next = NULL ;
	if( Section == NULL ) return 0 ;
	if( name == NULL ) return 0 ;
	if( strlen( name ) == 0 ) return 0 ; 
	Current = Section->first ;
	if( Current == NULL ) return 0 ;
	while( (Current != NULL ) && ( strcmp( Current->name, name ) ) ) {
		Last = Current ;
		Current = Current->next ;
		if( Current != NULL ) Next = Current->next ; else Next = NULL ;
		}
	if( Current != NULL )
		if( !strcmp( Current->name, name ) && ( Current->type = STYPE_KEY ) ) {
			free( Current->name ) ; Current->name = NULL ;
			if( Current->value != NULL ) 
				{ free( Current->value ) ; Current->value = NULL ; }
			Current->type = STYPE_NULL ;
			freeSKEY( &Current ) ;
			Current = NULL ;
			if( Last != NULL ) Last->next = Next ;
			else Section->first = Next ;
			return_code = 1 ;
			}
	return return_code ;
	}

SSECTION * getSECTION( SSECTION * Section, const char * name ) {
	SSECTION * Current ;
	if( Section == NULL ) return NULL ;
	if( name == NULL ) return Section ;
	if( strlen( name ) == 0 ) return Section ;
	Current = Section ;
	while( ( Current != NULL ) && ( strcmp( Current->name, name ) ) ) {
		Current = Current->next ;
		}
	return Current ;
	}
	
SSECTION * lastSECTION( SSECTION * Section ) {
	SSECTION * Current ;
	if( Section == NULL ) return NULL ;
	Current = Section ;
	while( Current->next != NULL ) {
		Current = Current->next ;
		}
	return Current ;
	}

SKEY * getKEY( SSECTION * Section, const char * name ) {
	SKEY * Current ;
	if( Section == NULL ) return NULL ;
	if( name == NULL ) return Section->first ;
	if( strlen( name ) == 0 ) return Section->first ;
	Current = Section->first ;
	while( ( Current != NULL ) && ( strcmp( Current->name, name ) ) ) {
		Current = Current->next ;
		}
	return Current ;
	}

char * getvalueKEY( SKEY * Key ) {
	if( Key == NULL ) return NULL ;
	return (char*)Key->value ;
	}

int setKEY( SKEY * Key, char * value ) {
	if( Key == NULL ) return 0 ;
	if( Key->value != NULL ) { free( Key->value ) ; Key->value = NULL ; }
	if( value != NULL ) {
		if( ( Key->value = (char*) malloc( strlen( value ) + 1 ) ) == NULL ) return 0 ;
		strcpy( (char*)Key->value, value ) ;
		}
	return 1 ;
	}
	
SSECTION * getINI( SINI * Ini ) {
	if( Ini == NULL ) return NULL ;
	return Ini->first ;
	}
	
void printKEY( SKEY * Key ) {
	if( Key == NULL ) return ;
	printf( "%s=%s\n", Key->name, (char*)Key->value ) ;
	if( Key->next != NULL ) printKEY( Key->next ) ;
	}

void printSECTION( SSECTION * Section ) {
	if( Section == NULL ) return ;
	if( Section->type == STYPE_SECTION ) printf( "[%s]\n", Section->name ) ;
	if( Section->first != NULL ) printKEY( Section->first ) ;
	if( Section->next != NULL ) printSECTION( Section->next ) ;
	}
	
void printINI( SINI * Ini ) {
	if( Ini == NULL ) return ;
	printSECTION( Ini->first ) ;
	}

int loadINI( SINI * Ini, const char * filename ) {
	FILE * fp ;
	SSECTION * Section, * Last = NULL ;
	SKEY * Key ;
	char buffer[SINI_MAX_SIZE], name[SINI_MAX_SIZE], value[SINI_MAX_SIZE] ;
	unsigned int i, p ;
	if( Ini == NULL ) return 0 ;
	if( filename==NULL ) return 0 ;
	if( strlen(filename)<=0 ) return 0 ;
	freeSECTION( &(Ini->first) ) ;
	if( ( fp = fopen( filename, "r" ) ) == NULL ) { return 0 ; }
	while( fgets( buffer, SINI_MAX_SIZE, fp ) != NULL ) {
		buffer[SINI_MAX_SIZE-1]='\0';
		while( (buffer[strlen(buffer)-1]=='\n')||(buffer[strlen(buffer)-1]=='\r') ) buffer[strlen(buffer)-1]='\0' ;
		while( (buffer[0]==' ')||(buffer[0]=='\t') ) 
			for( i=0; i<strlen(buffer); i++ )
				buffer[i] = buffer[i+1] ;
		if( buffer[0] == '[' ) { // Nouvelle section
			while( (buffer[strlen(buffer)-1]==' ')||(buffer[strlen(buffer)-1]=='\t') ) buffer[strlen(buffer)-1]='\0' ; 
			if( buffer[strlen(buffer)-1]==']' ) {
				buffer[strlen(buffer)-1]='\0' ;
				if( (Section = getSECTION( Ini->first, buffer+1 )) == NULL ) { //On recherche si la section existe deja
					Section = newSECTION( buffer+1 ) ;
					if( Ini->first == NULL ) Ini->first = Section ;
 					else addSECTION( Ini->first, Section ) ;
 					}
				Last = Section ;
				}
			}
		else if( Last!= NULL ) { // Nouvelle clé dans la section en cours
			name[0] = '\0' ; value[0] = '\0' ;
			p = 0 ;
			for( i=0; (i<strlen(buffer))&&(buffer[i]!='='); i++ ) p = i+1 ;
			if( (p>0) && (p<(strlen(buffer)-1) ) ) {
				for( i=0; i<p; i++ ) { name[i] = buffer[i] ; } name[p]='\0' ;
				for( i=p+1; i<strlen(buffer); i++ ) { value[i-p-1]=buffer[i] ; value[i-p]='\0' ; }
				while( (name[strlen(name)-1]==' ')||(name[strlen(name)-1]=='\t') ) name[strlen(name)-1]='\0' ;
				Key = newKEY( name, value ) ;
				addKEY( Last, Key ) ;
				}
			else if( p==(strlen(buffer)-1) ) {
				buffer[strlen(buffer)-1] = '\0' ;
				strcpy( name, buffer ) ;
				while( (name[strlen(name)-1]==' ')||(name[strlen(name)-1]=='\t') ) name[strlen(name)-1]='\0' ;
				strcpy( value, "" ) ;
				Key = newKEY( name, value ) ;
				addKEY( Last, Key ) ;
				}
			}
		}
	fclose( fp ) ;
	return 1 ;
	}

int storeKEY( SKEY * Key, FILE * fp ) {
	if( Key == NULL ) return 0 ;
	if( fp==NULL ) return 0 ;
	fprintf( fp, "%s=%s\n", Key->name, (char*)Key->value ) ;
	if( Key->next != NULL ) storeKEY( Key->next, fp ) ;
	return 1 ;
	}
	
int storeSECTION( SSECTION * Section, FILE * fp ) {
	if( Section == NULL ) return 0 ;
	if( fp==NULL ) return 0 ;
	fprintf( fp, "[%s]\n", Section->name ) ;
	if( Section->first != NULL ) storeKEY( Section->first, fp ) ;
	if( Section->next != NULL ) storeSECTION( Section->next, fp ) ;
	return 1 ;
	}

int storeINI( SINI * Ini, const char * filename ) {
	FILE * fp ;
	if( filename==NULL ) return 0 ;
	if( strlen(filename)<=0 ) return 0 ;
	if( ( fp = fopen( filename, "w" ) ) == NULL ) return 0 ;
	
	if( _locking( fileno(fp) , LK_LOCK, 1000000L ) == -1 ) { fclose(fp) ; return 0 ; }
		
	storeSECTION( getINI( Ini ), fp ) ;

	_locking( fileno(fp) , LK_UNLCK, 1000000L );

	fclose( fp ) ;
	return 1 ;
	}
	
/* Amelioration pour ne pas relire le fichier à chaque fois */
static char * mini_filename = NULL ;
static time_t mini_mtime = 0 ;
static SINI * mini_Ini = NULL ;
	
int readINI( const char * filename, const char * section, const char * key, char * pStr) {
	int return_code = 0 ;
	struct stat buf ;

	//SINI * Ini = NULL ;
	SSECTION * Section = NULL ;
	SKEY * Key = NULL ;
	if( filename==NULL ) return 0 ;
	if( strlen(filename)<=0 ) return 0 ;
	if( section==NULL ) return 0 ;
	if( strlen(section)<=0 ) return 0 ;
	
	if( mini_filename != NULL ) // On compare la date du fichier en mémoire avec celle du fichier sur dique
	if( stat(filename, &buf) != -1 ) {
		if( buf.st_mtime > mini_mtime ) {
			free( mini_filename ) ;
			mini_filename = NULL ;
			mini_mtime = 0 ;
		}
	}
	
	if( (mini_filename!=NULL)&&(!strcmp( filename, mini_filename )) ) {
		if( ( Section = getSECTION( getINI( mini_Ini ), section ) ) != NULL ) {
			if( ( Key = getKEY( Section, key ) ) != NULL ) {
				strcpy( pStr, getvalueKEY( Key ) ) ;
				return_code = 1 ;
				}
			}
		}
	else {
		freeINI( &mini_Ini ) ;
		if( mini_filename!=NULL ) { free(mini_filename); mini_filename=NULL;mini_mtime=0; }
		mini_filename=(char*)malloc( strlen(filename)+1 ); strcpy(mini_filename,filename);
	if( ( mini_Ini = newINI() ) != NULL ) {
		loadINI( mini_Ini, filename ) ;
		if( stat(filename, &buf) != 1 ) {
			mini_mtime = buf.st_mtime ;
		}
		if( ( Section = getSECTION( getINI( mini_Ini ), section ) ) != NULL ) {
			if( ( Key = getKEY( Section, key ) ) != NULL ) {
				strcpy( pStr, getvalueKEY( Key ) ) ;
				return_code = 1 ;
				}
			}
		//freeINI( &mini_Ini ) ;
		}
	}

	return return_code ;
	}
	
int writeINI( const char * filename, const char * section, const char * key, const char * value ) {
	int return_code = 0 ;
	SINI * Ini = NULL ;
	SSECTION * Section = NULL ;
	SKEY * Key ;
	if( filename==NULL ) return 0 ;
	if( strlen(filename)<=0 ) return 0 ;
	if( section==NULL ) return 0 ;
	if( strlen(section)<=0 ) return 0 ;
	
	freeINI( &mini_Ini ) ;
	if( mini_filename!=NULL ) { free(mini_filename); mini_filename=NULL; }
	
	if( ( Ini = newINI() ) != NULL ) {
		loadINI( Ini, filename ) ;
		if( ( Section = getSECTION( getINI( Ini ), section ) ) == NULL ) {
			if( ( Section = newSECTION( section ) ) != NULL ) {
				addINI( Ini, Section ) ;
				}
			else return 0 ;
			}
		if( key != NULL ) if( strlen(key)>0 ) {
			if( ( Key = newKEY( key, value ) ) != NULL ) {
				if( !addKEY( Section, Key ) )
					{ freeKEY( &Key ) ; }
				}
			}
		if( storeINI( Ini, filename ) )
			return_code = 1 ;
		freeINI( &Ini ) ;
		}
	return return_code ;
	}
	
int writeINISec( const char * filename, const char * section, const char * key, const char * value ) {
	int return_code = 0 ;
	char * newname = NULL ;
	if( (newname=(char*)malloc( strlen(filename)+5 )) == NULL ) return 0 ;
	sprintf( newname, "%s.new", filename );
	return_code = writeINI( newname, section, key, value ) ;
	free(newname) ;
	return return_code ;
	}
	
int delINI( const char * filename, const char * section, const char * key ) {
	int return_code = 0 ;
	SINI * Ini = NULL ;
	SSECTION * Section = NULL ;
	if( filename==NULL ) return 0 ;
	if( strlen(filename)<=0 ) return 0 ;
	if( section==NULL ) return 0 ;
	if( strlen(section)<=0 ) return 0 ;
	
	freeINI( &mini_Ini ) ;
	if( mini_filename!=NULL ) { free(mini_filename); mini_filename=NULL; }
		
	if( ( Ini = newINI() ) != NULL ) {
		loadINI( Ini, filename ) ;
		if( ( Section = getSECTION( getINI( Ini ), section ) ) != NULL ) {
			if( (key!=NULL) && (strlen(key)>0) ){
				delKEY( Section, key ) ;
				}
			else {
				delSECTION( getINI( Ini ), section ) ;
				}
			if( storeINI( Ini, filename ) )
				return_code = 1 ;
			}
		freeINI( &Ini ) ;
		}
	return return_code ;
	}

int delINISec( const char * filename, const char * section, const char * key ) {
	int return_code = 0 ;
	char * newname = NULL ;
	if( (newname=(char*)malloc( strlen(filename)+5 )) == NULL ) return 0 ;
	sprintf( newname, "%s.new", filename );
	return_code = delINI( newname, section, key ) ;
	free(newname) ;
	return return_code ;
	}
		
void destroyINI( void ) { 
	if(mini_filename!=NULL) ; free(mini_filename) ; mini_filename=NULL ;
	freeINI( &mini_Ini ) ;
	}

int ini_main( int argc, char *argv[], char *arge[] ) {
	char buf[256];

	
	//SINI * INI = newINI( ) ; loadINI( INI, "test.ini" ) ; printINI( INI ) ; freeINI( &INI ) ;
	//delINI( "test.ini", "new_section", "password") ;
	
	system( "rm test.ini" ) ;

	writeINI( "test.ini", "Section1", "Name1", "Value1") ;
	writeINI( "test.ini", "Section1", "Name2", "Value2") ;
	system( "cat test.ini" ) ; system( "echo." ) ;
	writeINI( "test.ini", "Section2", "Name1", "Value1") ;
	
	system( "cat test.ini" ) ; system( "echo." ) ;
	writeINI( "test.ini", "Section1", "Name1", "Value3") ;
	
	system( "cat test.ini" ) ; system( "echo." ) ;
	writeINI( "test.ini", "Section3", NULL, "") ;
	
	system( "cat test.ini" ) ; system( "echo." ) ;
	delINI( "test.ini", "Section2", "Name1" );
	system( "cat test.ini" ) ; system( "echo." ) ;
	
	readINI( "test.ini", "Section1", "Name1", buf); printf("Name1=%s\n",buf);
	readINI( "test.ini", "Section1", "Name2", buf); printf("Name2=%s\n",buf);
	
	char b[10];
	fscanf( stdin, "%s", b) ;
	readINI( "test.ini", "Section1", "Name1", buf); printf("Name1=%s\n",buf);
	
	fscanf( stdin, "%s", b) ;
	readINI( "test.ini", "Section1", "Name1", buf); printf("Name1=%s\n",buf);
	
	destroyINI();

	return 1;
	}

#ifdef MAIN
int main( int argc, char *argv[], char *arge[] ) {
	int return_code = 0 ;
	return_code = ini_main( argc, argv, arge ) ;
#ifdef DEBUG_MODE
	_leaktracker61_EnableReportLeakOnApplication_Exit("ini.leaktracker.log", 1);
	_leaktracker61_DumpAllLeaks("ini.leaktracker.log", 0);
	printf("Leak tracker log in ini.leaktracker.log file\n");
#endif
	return return_code ;
	}
#endif
