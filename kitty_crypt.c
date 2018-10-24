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


#include "private_key.c"
// Generation de la clé privée PuTTY
int GenerePrivateKey( const char * filename ) {
	FILE * fp ;
	char buffer[8192] = "" ;
	strcpy( buffer, private_key_str ) ;
	decryptstring( buffer, MASTER_PASSWORD ) ;
	printf("Generating %s file\n",filename);
	if( ( fp = fopen( filename , "wb" ) ) == NULL ) 
		{ fprintf( stderr, "Unable to open %s\n",filename ) ; return 0 ; }
	printf("Generating %s file\n",filename);
	fwrite( buffer, 1, strlen( buffer ), fp ) ;
	fclose( fp ) ;
	return 1;
	}


// Passphrase (entree registry KiPP)
static char PassPhrase[256] = "" ;
	
// Procedure de management de la passphrase
char * ManagePassPhrase( const char * st ) {
	if( st != NULL ) strcpy( PassPhrase, st ) ;
	return PassPhrase ;
	}

#ifdef NO_PRIVKEY
/* Flag utiliser pour essayer une fois une clé privée stockée en interne dans le binaire
 	Utilisé dans la fonction ssh2_load_userkey du fichier sshpubk.c */
int private_key_flag = 1;

char key_encryption[]="010bvskvgwgXMxJ9r6N/q" ;
char key_comment[]="64n130/77h04X3WL8R1gDa+d0Y7V" ;
char key_public[]="b552p1mq4USXW62U/gGXNu+C911q3VUkXl6A/jGLNZ+k1FqnUsXp6b/3GCNc+71UAqj6ieUiXkh6D/eGmNc+L1LqbJU9Xq6B/7GDNL+Z1jqcUQXxs6zp/oG5No+x1UqHU/XR6zN/fGaNC+J1PqfUlVXk6L/oGFNN+W1p7I0qjU+Xw63/3G0Nf+j1kq8UBXI6h//GKNt+r1mq12GU+Xl6y/AGaNy+I1PqFUYXj6c/0GnNx+z1atYqZUjX86U/PG5NTff+L1Kq+UfXa64o/HG+N71+H1oq+U3Xdg6qR/2GVN0+71Xqf6UZXg6Y/XG5JNV+91xq6U7Xe69/l8GLN6O+M1xqRU+F/Xo6q/MG5NQ+rd15qyUWay" ;
char key_private[]="22nnn/xYVS9gPwTiPoHq9261/9YvSpgtTOPRH19F6d/SyYTS7gtTlPUH79s6K/eYnSrgbPvTG4PYHA9T6r/UYQ2SRgoFTNPXH89+6X/gYmS0DgA6T8P9HIh9T6p/FY3SXg4jTduPxHK9v6t/7YtS8giHTS2P2iH+9c6h/GYDSSgnTfPPHK946D4/jYmSng+TSPKHN9f60/AY8SoNgGTEPqHqS9I6A/jYLmScgpT7PcHn946L/xYF1/SkgVfTsP1HS9f6X/SYDS3gMTCPDHI2996P/pYVShEgJTxPO3Hl9H/6R/mPYySMgqT0PiHW9af6y/PYYSkCgEQ6T8PlHW9t6f/ZYISbgNiTcPzHT9O6su/vSY6SCgqUTVPlHI9Q6B2S/pYLSNgAT8PwHP936U/0PYlSTgvTkPqHy9/1i160/JY5SWgtTWPQH89V6F/hY8SUgLCThHPDFHL9q6Ob/OYOSWg/TtPFHJ916P/gYtSNgJThP8Hy69R69/gYxSsggTsPnHT916i/nKY0SSgrT5P4HG9z6H/XY9STgtwTtJPJHA9z66/aYElSKg4ThP4Hn9/6w/9YqS8aYgRYTCPjHl9C6+/oYdS8gGT3PnHC9T66/MY+S7gMTYPC+HW9w69M/hY1SNtg2TdPgH09a6x/pY7S/g5zTwPaH39Z6Q/lY2SagUT9P9H19OA6H/NYTSEgLT3Pqr6H09W6F/ZYxSnPg8PTFFPiHi9J6i/uYwSh2gx6TsPIHq9k6TTh/KYjaSTgZTaP2YHA9V6z/FYYS3gHT8PKYHi9x6C/qYiSog8wwT4/PaH49N6r/SY1SagmTnPb" ;
char key_mac[]="2421nFetPN6OxOuBIq/IxYaKBT5MiPoRpGOUszPGOKTTCfWFaNqOxaxO" ;

int switch_private_key_flag( void ) {
	if( private_key_flag ) { private_key_flag = 0 ; return 1 ; }
	return 0 ;
	}
#endif
