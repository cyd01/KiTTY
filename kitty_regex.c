#include "kitty_regex.h"

int strgrep( const char * pattern, const char * str ) {
	int return_code = 1 ;
	regex_t preg ;

	if( (return_code = regcomp (&preg, pattern, REG_NOSUB | REG_EXTENDED ) ) == 0 ) {
		return_code = regexec( &preg, str, 0, NULL, 0 ) ;
		regfree( &preg ) ;
		}

	return return_code ;
	}
