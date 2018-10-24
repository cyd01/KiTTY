#include "kitty_tools.h"

char *stristr (const char *meule_de_foin, const char *aiguille) {
	char *c1, *c2, *res = NULL ; int i ;
	c1=(char*)malloc( strlen(meule_de_foin) + 1 ) ; strcpy( c1, meule_de_foin ) ;
	c2=(char*)malloc( strlen(aiguille) + 1 ) ; strcpy( c2, aiguille ) ;
	if( strlen(c1)>0 ) {for( i=0; i<strlen(c1); i++ ) c1[i]=toupper( c1[i] ) ;}
	if( strlen(c2)>0 ) {for( i=0; i<strlen(c2); i++ ) c2[i]=toupper( c2[i] ) ;}
	res=strstr(c1,c2);
	if( res!=NULL ) res = (char*)(meule_de_foin+( res-c1 )) ;
	free( c2 ) ;
	free( c1 ) ;
	return res ;
	}

/* Fonction permettant d'inserer une chaine dans une autre */
int insert( char * ch, const char * c, const int ipos ) {
	int i = ipos, len = strlen( c ), k ;
	if( ( ch == NULL ) || ( c == NULL ) ) return -1 ;
	if( len > 0 ) {
		if( (size_t) i > ( strlen( ch ) + 1 ) ) i = strlen( ch ) + 1 ;
		for( k = strlen( ch ) ; k >= ( i - 1 ) ; k-- ) ch[k + len] = ch[k] ;
		for( k = 0 ; k < len ; k++ ) ch[k + i - 1] = c[k] ; 
		}
	return strlen( ch ) ; 
	}

/* Fonction permettant de supprimer une partie d'une chaine de caracteres */
int del( char * ch, const int start, const int length ) {
	int k, len = strlen( ch ) ;
	if( ch == NULL ) return -1 ;
	if( ( start == 1 ) && ( length >= len ) ) { ch[0] = '\0' ; len = 0 ; }
	if( ( start > 0 ) && ( start <= len ) && ( length > 0 ) ) {
		for( k = start - 1 ; k < ( len - length ) ; k++ ) {
			if( k < ( len - length ) ) ch[k] = ch[ k + length ] ;
			else ch = '\0' ; 
			}
		k = len - length ;
		if( ( start + length ) > len ) k = start - 1 ;
		ch[k] = '\0' ; 
		}
	return strlen( ch ) ; 
	}

/* Fonction permettant de retrouver la position d'une chaine dans une autre chaine */
int poss( const char * c, const char * ch ) {
	char * c1 , * ch1 , * cc ;
	int res ;
	if( ( ch == NULL ) || ( c == NULL ) ) return -1 ;
	if( ( c1 = (char *) malloc( strlen( c ) + 1 ) ) == NULL ) return -2 ;
	if( ( ch1 = (char *) malloc( strlen( ch ) + 1 ) ) == NULL ) { free( c1 ) ; return -3 ; }
	strcpy( c1, c ) ; strcpy( ch1, ch ) ;
	cc = (char *) strstr( ch1, c1 ) ;
	if( cc == NULL ) res = 0 ;
	else res = (int) ( cc - ch1 ) + 1 ;
	if( (size_t) res > strlen( ch ) ) res = 0 ;
	free( ch1 ) ;
	free( c1 ) ;
	return res ; 
	}
	
/* Fonction permettant de retrouver la position d'une chaîne de caracteres dans une chaine a partir d'une position donnee */
int posi( const char * c, const char * ch, const int ipos ) {
	int res ;
	if( ( c == NULL ) || ( ch == NULL ) ) return -1 ;
	if( ( ipos <= 0 ) || ( (size_t) ipos > strlen( ch ) ) ) return 0 ;
	res = poss( c, ch + ( ipos - 1 ) ) ;
	if( res > 0 ) return res + ( ipos -1 ) ;
	else return 0 ;
	}

// Teste l'existance d'un fichier
int existfile( const char * filename ) {
	struct _stat statBuf ;
	
	if( filename == NULL ) return 0 ;
	if( strlen(filename)==0 ) return 0 ;
	if( _stat( filename, &statBuf ) == -1 ) return 0 ;
	
	if( ( statBuf.st_mode & _S_IFMT ) == _S_IFREG ) { return 1 ; }
	else { return 0 ; }
	}
	
// Teste l'existance d'un repertoire
int existdirectory( const char * filename ) {
	struct _stat statBuf ;
	
	if( filename == NULL ) return 0 ;
	if( strlen(filename)==0 ) return 0 ;
	if( _stat( filename, &statBuf ) == -1 ) return 0 ;
	
	if( ( statBuf.st_mode & _S_IFMT ) == _S_IFDIR ) { return 1 ; }
	else { return 0 ; }
	}

/* Donne la taille d'un fichier */
long filesize( const char * filename ) {
	FILE * fp ;
	long length ;

	if( filename == NULL ) return 0 ;
	if( strlen( filename ) <= 0 ) return 0 ;
	
	if( ( fp = fopen( filename, "r" ) ) == 0 ) return 0 ;
	
	fseek( fp, 0L, SEEK_END ) ;
	length = ftell( fp ) ;
	
	fclose( fp ) ;
	return length ;
	}

// Supprime les double anti-slash
void DelDoubleBackSlash( char * st ) {
	int i=0,j ;
	while( st[i] != '\0' ) {
		if( (st[i] == '\\' )&&(st[i+1]=='\\' ) ) {
			for( j=i+1 ; j<strlen( st ) ; j++ ) st[j]=st[j+1] ;
			}
		else i++ ;
		}
	}

// Ajoute une chaine dans une liste de chaines
int StringList_Add( char **list, const char * name ) {
	int i = 0 ;
	if( name == NULL ) return 1 ;
	while( list[i] != NULL ) {
		if( !stricmp( name, list[i] ) ) return 1 ;
		i++ ;
		}
	if( ( list[i] = (char*) malloc( strlen( name ) + 1 ) ) == NULL ) return 0 ;
	strcpy( list[i], name ) ;
	list[i+1] = NULL ;
	return 1 ;
	}

// Test si une chaine existe dans une liste de chaines
int StringList_Exist( const char **list, const char * name ) {
	int i = 0 ;
	while( list[i] != NULL ) {
		if( strlen( list[i] ) > 0 )
			if( !strcmp( list[i], name ) ) return 1 ;
		i++ ;
		}
	return 0 ;
	}
	
// Supprime une chaine d'une liste de chaines
void StringList_Del( char **list, const char * name ) {
	int i = 0 ;
	while( list[i] != NULL ) {
		if( strlen( list[i] ) > 0 )
			if( !strcmp( list[i], name ) ) {
				strcpy( list[i], "" ) ;
				}
		i++;
		}
	}

// Reorganise l'ordre d'une liste de chaines en montant la chaine selectionnee d'un cran
void StringList_Up( char **list, const char * name ) {
	char *buffer ;
	int i = 0 ;
	while( list[i] != NULL ) {
		if( !strcmp( list[i], name ) ) {
			if( i > 0 ) {
				buffer=(char*)malloc( strlen(list[i-1])+1 ) ;
				strcpy( buffer, list[i-1] ) ;
				free( list[i-1] ) ; list[i-1] = NULL ;
				list[i-1]=(char*)malloc( strlen(list[i])+1 ) ;
				strcpy( list[i-1], list[i] ) ;
				free( list[i] ) ;
				list[i] = (char*) malloc( strlen( buffer ) +1 ) ;
				strcpy( list[i], buffer );
				free( buffer );
				}
			return ;
			}
		i++ ;
		}
	}

// Positionne l'environnement
int putenv (const char *string) ;
int set_env( char * name, char * value ) {
	int res = 0 ;
	char * buffer = NULL ;
	if( (buffer = (char*) malloc( strlen(name)+strlen(value)+2 ) ) == NULL ) return -1 ;
	sprintf( buffer,"%s=%s", name, value ) ; 
	res = putenv( (const char *) buffer ) ;
	free( buffer ) ;
	return res ;
	}
int add_env( char * name, char * value ) {
	int res = 0 ;
	char * npst = getenv( name ), * vpst = NULL ;
	if( npst==NULL ) { res = set_env( name, value ) ; }
	else {
		vpst = (char*) malloc( strlen(npst)+strlen(value)+20 ) ; 
		sprintf( vpst, "%s=%s;%s", name, npst, value ) ;
		res = set_env( name, vpst ) ;
		free( vpst ) ;
		}
	return res ;
	}

// Creer un repertoire recurssif (rep1 / rep2 / ...)
int _mkdir (const char*);
int MakeDir( const char * directory ) {
	char buffer[MAX_VALUE_NAME], fullpath[MAX_VALUE_NAME], *p, *pst ;
	int i,j ;
	
	if( directory==NULL ) return 1 ; if( strlen(directory)==0 ) return 1 ;

	for( i=0, j=0 ; i<=strlen(directory) ; i++,j++ ) { // On supprime les espaces après un '\' 
		if( (directory[i]=='\\')||(directory[i]=='/') ) {
			fullpath[j]='\\' ;
			while( (directory[i+1]==' ')||(directory[i+1]=='	') ) i++ ;
			}
		else fullpath[j]=directory[i] ;
		}
	fullpath[j+1]='\0' ;
		
	// On supprime les espaces, les / et les \\ à la fin
	while( (fullpath[strlen(fullpath)-1]==' ')||(fullpath[strlen(fullpath)-1]=='	')||(fullpath[strlen(fullpath)-1]=='/')||(fullpath[strlen(fullpath)-1]=='\\') ) fullpath[strlen(fullpath)-1]='\0';

	for( i=strlen(fullpath), j=strlen(fullpath) ; i>=0 ; i--, j-- ) { // On supprime les espaces avant un '\'
		if( fullpath[i] == '\\' ) {
			buffer[j]='\\' ;
			while( (i>0)&&((fullpath[i-1]==' ')||(fullpath[i-1]=='	')) ) i-- ;
			}
		else buffer[j]=fullpath[i] ;
		}
	j++;
		
	// On supprime les espace au début
	while( ((buffer+j)[0]==' ')||((buffer+j)[0]=='	') ) j++ ;
	strcpy( fullpath, buffer+j ) ;
	
	// On crée les répertoires
	if( !existdirectory(fullpath) ) {
		pst = fullpath ;
		while( (strlen(pst)>0)&&((p=strstr(pst,"\\"))!=NULL) ) {
			p[0]='\0' ;
			_mkdir( fullpath ) ;
			p[0]='\\' ;
			pst=p+1;
			}
		_mkdir( fullpath ) ;
	}
		
	return existdirectory(fullpath) ;
	}
