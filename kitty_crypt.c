#include "kitty_crypt.h"

int cryptstring( const int mode, char * st, const char * key ) {
	if( mode>1 ) return strlen(st);
	if( strlen(st)==0 ) { return 0 ; }
	return bcrypt_string_base64( st, st, strlen( st ), key, 0 ) ; 
}

int decryptstring( const int mode, char * st, const char * key ) { 
	if( mode>1 ) return strlen(st);
	if( strlen(st)==0 ) { return 0 ; }
	int res = buncrypt_string_base64( st, st, strlen( st ), key ) ; 
	if( res == 0 ) strcpy( st, "" ) ;
	return res ;
	}

void dopasskey( int mode, char * passkey, const char * host, const char * termtype ) {
    if( mode > 0 ) {
	strcpy( passkey, "KiTTY" ) ;
    } else {
	if( strlen(host)>0 ) {
		if( (strlen(host)+strlen(termtype)) < 1000 ) { 
			sprintf( passkey, "%s%sKiTTY", host, termtype ) ;
		} else {
			strcpy( passkey, "" ) ;
		}
	} else { 
		if( strlen(termtype) < 1000 ) { 
			sprintf( passkey, "%sKiTTY", termtype ) ;
		} else {
			strcpy( passkey, "" ) ;
		}
	}
    }
}

int cryptpassword( const int mode, char * password, const char * host, const char * termtype ) {
	char PassKey[1024] = "" ;
	int ret ;
	dopasskey( mode, PassKey, host, termtype ) ;
	ret = cryptstring( mode, password, PassKey ) ;
	return ret ;
}

int decryptpassword( const int mode, char * password, const char * host, const char * termtype ) {
	char PassKey[1024] = "" ;
	int ret ;
	dopasskey( mode, PassKey, host, termtype ) ;
	ret = decryptstring( mode, password, PassKey ) ;
	return ret ;
}

//static char MASKKEY[128] = MASTER_PASSWORD ;
static char MASKKEY[128] = "¤¥©ª³¼½¾" ;

void MASKPASS( const int mode, char * password ) {
	if( mode > 0 ) return ;
	if( password==NULL ) return ;
	if( strlen(password)==0) return ;
	
	int i,j=0, len=strlen(password) ;
	char c, *buffer ;
	buffer=(char*)malloc(strlen(password)+1);
	buffer[0]='\0' ;
	if( (len>0)&&(strlen(MASKKEY)>0) )
	for( i=0 ; i<len ; i++ ) {
		c = password[i]^MASKKEY[j] ;
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



/* ISO-8859-1 to UTF-8 mapper 
 * return 0 for success, and need buffer size otherwise
 */
size_t iso8859_1_to_utf8(char *content, size_t max_size) {
//https://codereview.stackexchange.com/questions/40780/function-to-convert-iso-8859-1-to-utf-8
    char *src, *dst;

    //first run to see if there's enough space for the new bytes
    for (src = dst = content; *src; src++, dst++) {
        if (*src & 0x80) {
            // If the high bit is set in the ISO-8859-1 representation, then
            // the UTF-8 representation requires two bytes (one more than usual).
            ++dst;
        }
    }

    if (dst - content + 1 > max_size) {
        // Inform caller of the space required
        return dst - content + 1;
    }

    *(dst + 1) = '\0';
    while (dst > src) {
        if (*src & 0x80) {
            *dst-- = 0x80 | (*src & 0x3f);                     // trailing byte
            *dst-- = 0xc0 | (*((unsigned char *)src--) >> 6);  // leading byte
        } else {
            *dst-- = *src--;
        }
    }
    return 0;  // SUCCESS
}

/* UTF-8 to ISO-8859-1/ISO-8859-15 mapper.
* Return 0..255 for valid ISO-8859-15 code points, 256 otherwise.
*/
static inline unsigned int to_latin9(const unsigned int code) {
    /* Code points 0 to U+00FF are the same in both. */
    if (code < 256U)
        return code;
    switch (code) {
        case 0x0152U: return 188U; /* U+0152 = 0xBC: OE ligature */
        case 0x0153U: return 189U; /* U+0153 = 0xBD: oe ligature */
        case 0x0160U: return 166U; /* U+0160 = 0xA6: S with caron */
        case 0x0161U: return 168U; /* U+0161 = 0xA8: s with caron */
        case 0x0178U: return 190U; /* U+0178 = 0xBE: Y with diaresis */
        case 0x017DU: return 180U; /* U+017D = 0xB4: Z with caron */
        case 0x017EU: return 184U; /* U+017E = 0xB8: z with caron */
        case 0x20ACU: return 164U; /* U+20AC = 0xA4: Euro */
        default:      return 256U;
    }
}

/* Convert an UTF-8 string to ISO-8859-15.
* All invalid sequences are ignored.
* Note: output == input is allowed,
* but   input < output < input + length
* is not.
* Output has to have room for (length+1) chars, including the trailing NUL byte.
*/
size_t utf8_to_iso8859_15(char *const output, const char *const input, const size_t length) {
//https://askcodez.com/est-il-un-moyen-de-convertir-en-utf-8-en-iso-8859-1.html
    unsigned char             *out = (unsigned char *)output;
    const unsigned char       *in  = (const unsigned char *)input;
    const unsigned char *const end = (const unsigned char *)input + length;
    unsigned int               c;
    while (in < end)
        if (*in < 128)
            *(out++) = *(in++); /* Valid codepoint */
        else
            if (*in < 192)
                in++;               /* 10000000 .. 10111111 are invalid */
            else
                if (*in < 224) {        /* 110xxxxx 10xxxxxx */
                    if (in + 1 >= end)
                    break;
                    if ((in[1] & 192U) == 128U) {
                        c = to_latin9( (((unsigned int)(in[0] & 0x1FU)) << 6U)
                            |  ((unsigned int)(in[1] & 0x3FU)) );
                        if (c < 256)
                            *(out++) = c;
                    }
                    in += 2;
                } else
                    if (*in < 240) {        /* 1110xxxx 10xxxxxx 10xxxxxx */
                        if (in + 2 >= end)
                            break;
                        if ((in[1] & 192U) == 128U &&
                            (in[2] & 192U) == 128U) {
			    c = to_latin9( (((unsigned int)(in[0] & 0x0FU)) << 12U)
                                | (((unsigned int)(in[1] & 0x3FU)) << 6U)
                                |  ((unsigned int)(in[2] & 0x3FU)) );
                            if (c < 256)
                                *(out++) = c;
                        }
                        in += 3;
                    } else
                        if (*in < 248) {        /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
                            if (in + 3 >= end)
                                break;
		    	    if ((in[1] & 192U) == 128U &&
                                (in[2] & 192U) == 128U &&
                                (in[3] & 192U) == 128U) {
                                    c = to_latin9( (((unsigned int)(in[0] & 0x07U)) << 18U)
                                        | (((unsigned int)(in[1] & 0x3FU)) << 12U)
                                        | (((unsigned int)(in[2] & 0x3FU)) << 6U)
                                        |  ((unsigned int)(in[3] & 0x3FU)) );
                                    if (c < 256)
                                        *(out++) = c;
			    }
                            in += 4;
                        } else
                            if (*in < 252) {        /* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
                                if (in + 4 >= end)
                                    break;
                                if ((in[1] & 192U) == 128U &&
                                    (in[2] & 192U) == 128U &&
                                    (in[3] & 192U) == 128U &&
                                    (in[4] & 192U) == 128U) {
                                    c = to_latin9( (((unsigned int)(in[0] & 0x03U)) << 24U)
                                        | (((unsigned int)(in[1] & 0x3FU)) << 18U)
                                        | (((unsigned int)(in[2] & 0x3FU)) << 12U)
                                        | (((unsigned int)(in[3] & 0x3FU)) << 6U)
                                        |  ((unsigned int)(in[4] & 0x3FU)) );
                                    if (c < 256)
                                        *(out++) = c;
				}
                                in += 5;
                            } else
                                if (*in < 254) {        /* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
                                    if (in + 5 >= end)
                                        break;
                                if ((in[1] & 192U) == 128U &&
                                    (in[2] & 192U) == 128U &&
                                    (in[3] & 192U) == 128U &&
				    (in[4] & 192U) == 128U &&
                                    (in[5] & 192U) == 128U) {
				    c = to_latin9( (((unsigned int)(in[0] & 0x01U)) << 30U)
                                            | (((unsigned int)(in[1] & 0x3FU)) << 24U)
                                            | (((unsigned int)(in[2] & 0x3FU)) << 18U)
                                            | (((unsigned int)(in[3] & 0x3FU)) << 12U)
                                            | (((unsigned int)(in[4] & 0x3FU)) << 6U)
                                            |  ((unsigned int)(in[5] & 0x3FU)) );
                                    if (c < 256)
                                        *(out++) = c;
				}
                                in += 6;
                            } else
                                in++;               /* 11111110 and 11111111 are invalid */
    /* Terminate the output string. */
    *out = '\0';
    return (size_t)(out - (unsigned char *)output);
}
