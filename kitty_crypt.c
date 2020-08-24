#include "kitty_crypt.h"
char PassKey[1024] ="" ;

int cryptstring( char * st, const char * key ) { return bcrypt_string_base64( st, st, strlen( st ), key, 0 ) ; }
int decryptstring( char * st, const char * key ) { 
	if( strlen(st)==0 ) { return 0 ; }
	int res = buncrypt_string_base64( st, st, strlen( st ), key ) ; 
	if( res == 0 ) strcpy( st, "" ) ;
	return res ;
	}

//static char MASKKEY[128] = MASTER_PASSWORD ;
static char MASKKEY[128] = "¤¥©ª³¼½¾" ;

void MASKPASS( char * password ) {
	
	//return ;    //   POUR SIMPLIFIER EN ATTENDANT QUE TOUT FONCTIONNE DANS LA MISE A JOUR > 2013/06/27
	if( password==NULL ) return ;
	if( strlen(password)==0) return ;
	
	int i,j=0, len=strlen(password) ;
	char c, *buffer ;
	buffer=(char*)malloc(strlen(password)+1);
	buffer[0]='\0' ;
	if( (len>0)&&(strlen(MASKKEY)>0) )
	for( i=0 ; i<len ; i++ ) {
		c=password[i]^MASKKEY[j] ;
		if( c==0 ) { free(buffer) ; return ; }
		buffer[i]=c ; buffer[i+1] = '\0' ;
		j++ ; if(MASKKEY[j]=='\0') j=0 ;
		}
	strcpy( password, buffer ) ;
	memset(buffer,0,strlen(password) );
	free(buffer) ;
	}

// Passphrase (entree registry KiPP)
static char PassPhrase[256] = "" ;
	
// Procedure de management de la passphrase
char * ManagePassPhrase( const char * st ) {
	if( !GetUserPassSSHNoSave() && (st != NULL) ) {
		strcpy( PassPhrase, st ) ;
	}
	if( GetUserPassSSHNoSave() ) { return "" ; }
	return PassPhrase ;
}
