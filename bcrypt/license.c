#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <libgen.h>
#include <string.h>
#include <strings.h>

// gcc -o license.exe license.c -DMAIN

long int license_modulo( const char * a, const int mod, const char sep ) {
	long int n = mod, m ;
	int size = 7, i, j ;
	char * buf = NULL, *s1, *s2 ;
	
	if( a==NULL ) return -1 ;
	if( strlen(a)==0 ) return 0 ;
	
	if( (buf=(char*)malloc(2*strlen(a)+3))==NULL ) return -1 ;
	if( (s1 =(char*)malloc(2*strlen(a)+3))==NULL ) { free(buf) ; return -1 ; }
	if( (s2 =(char*)malloc(2*strlen(a)+3))==NULL ) { free(s1) ; free(buf) ; return -1 ; }

	sprintf( buf, "%ld", n ) ;
	if( size <= strlen(buf) ) size=strlen(buf)+1 ;

	strcpy( buf, "" ) ;
	for(i=0,j=0;i<strlen(a);i++,j++) {
		if( (a[i]>='0')&&(a[i]<='9') ) { buf[j]=a[i]; }
		else if( (a[i]>='A')&&(a[i]<='J') ) { buf[j]='1';j++;buf[j]=(a[i]-'A'+'0'); }
		else if( (a[i]>='K')&&(a[i]<='T') ) { buf[j]='2';j++;buf[j]=(a[i]-'K'+'0'); }
		else if( (a[i]>='U')&&(a[i]<='Z') ) { buf[j]='3';j++;buf[j]=(a[i]-'U'+'0'); }
		else if( (a[i]==sep ) ) { j--; }
		else { buf[j]='0'; }
		buf[j+1]='\0' ;
		}

	while( strlen(buf) > size ) {
		for( i=0 ; i<size ; i++ ) { s1[i] = buf[i] ; } s1[size] = '\0' ;
		for( i=0 ; i<=(strlen(buf)-size) ; i++ ) s2[i] = buf[i+size] ;
		m = atol(s1) % n ;
		sprintf( buf, "%ld%s", m, s2 ) ;
		}
	free(s2);free(s1);
	m = atol(buf) % n ;
	free(buf) ;
		
	return m ;
	}

void license_init( void ) { 
	char buf[64], c ;
	int i ;
	time_t t = time(0) ;
	sprintf( buf, "%ld", (const long int)t ) ;
	for( i=0;i<(strlen(buf)/2);i++ ) { c=buf[i];buf[i]=buf[strlen(buf)-1-i];buf[strlen(buf)-1-i]=c;}
	srand( atol(buf) ) ; 
	} 

void license_add( char * license ) {
	int i, fin=0 ;
	if( license==NULL ) return ;
	if( strlen(license) == 0 ) { strcpy( license, "1" ) ; return ; }
	i = strlen(license)-1 ;
	while( (!fin) && (i>=0)  ) {
		if( (license[i]>='0')&&(license[i]<='8') ) { license[i]=license[i]+1; fin=1; }
		else if( license[i]=='9' ) { license[i]='A'; fin=1; }
		else if( (license[i]>='A')&&(license[i]<='Y') ) { license[i]=license[i]+1; fin=1; }
		else if( license[i]=='Z' ) { license[i]='0';  }
		i--;
		}
	if( !fin ) {
		for( i=strlen(license)+1 ; i>0 ; i-- ) license[i]=license[i-1] ;
		license[0]='1';
		}
	}

int license_make( char * license, int length, int modulo, int result ) {
	int i ;
	if( license==NULL ) return 0 ;
	if( result>=modulo ) result=0 ;
	license[length] = '\0' ;
	
	if( strlen(license)<length )	
		for( i=strlen(license); i<length ; i++ ) {
			license[i] = '0' + (int) ( 36.0 * (rand() / (RAND_MAX + 1.0))) ;
			if(license[i]>'9') license[i]=license[i]-'9'+'A'-1;
			}
	while( ((int)license_modulo( license, modulo, '-' )) != result ) {
		license_add( license ) ;
		}
	return 1 ;
	}

int license_make_with_first( char * license, int length, int modulo, int result ) {
	if( license==NULL ) return 0 ;
	if( (strlen(license)==0)||(license[0]<'A')||(license[0]>'Z') ) {
		license[0] = 'A' + (int) ( 26.0 * (rand() / (RAND_MAX + 1.0))) ;
		license[1] = '\0';
		}
	return license_make( license, length, modulo, result ) ;
	}

void license_form( char * license, char sep, int size ) {
	int i=1, j=1, k ;
	if( license==NULL ) return ;
	if( strlen(license) <=size ) return ;
	if( size<=0 ) return ;

	while( license[i]!='\0' ) {
		if( (j%size) == 0 ) {
			for( k=strlen(license)+1 ; k>i ; k-- ) license[k]=license[k-1] ;
			license[i]=sep ;
			i++ ;
			}
		i++; j++;
		}
	}

int license_test( char * license, char sep, int modulo, int result ) {
	if( (int)(license_modulo( license, modulo, sep )) == result ) return 1 ;
	return 0 ;
	}

void license_usage( char * progname ) {
	fprintf( stderr, "%s - License generator\n", basename(progname) ) ;
	fprintf( stderr, "Usage: %s [-b begining] [-g groupsize] [-l] [-m modulo] [-s size] [-t]\n", basename(progname) ) ;
	fprintf( stderr, "\t-b: the first letters of the license\n" ) ;
	fprintf( stderr, "\t-g: the size of each group\n" );
	fprintf( stderr, "\t-l: the first character is a letter\n" );
	fprintf( stderr, "\t-m: the modulo expected\n" );
	fprintf( stderr, "\t-s: the size of the license\n" );
	fprintf( stderr, "\t-t: to test a license value entered with -b parameter\n");
	}

int license_main( int argc, char *argv[], char *arge[] ){
	int return_code = 0, letter_flag = 0, group_size=0, test_flag=0 ;
	char *buf=NULL, opt, sep='-' ;
	
	int size=20, modulo=97;
		
	while ((opt = getopt(argc, argv, "b:g:hlm:s:t")) != -1)
		switch (opt) {
		case 'b':
			if( (buf=(char*)malloc(strlen(optarg)+1))==NULL ) return 1;
			strcpy( buf, optarg ) ;
			break;
		case 'g': 
			group_size = atoi(optarg) ;
			if( group_size<1 ) group_size = 1 ;
			break;
		case 'l':
			letter_flag = 1 ;
			break;
		case 'm': 
			modulo = atoi(optarg) ;
			if( modulo<2 ) modulo = 2 ;
			break;
		case 's':
			size = atoi(optarg) ;
			if( size<1 ) size=1 ;
			break;
		case 't':
			test_flag=1;
			break ;
		case 'h':
		case '?':
		default:
			license_usage(argv[0]); exit(0);
		}

	if (optind != argc) { license_usage(argv[0]); exit(0); }
	
	if( test_flag ) {
		return_code=license_test( buf, sep, modulo, 0 ) ;
		if( return_code ) { printf( "0\n" ) ; return 0 ; }
		else { printf( "%d\n", return_code ) ; return 1 ; }
		}
	
	if( buf==NULL ) { buf=(char*)malloc(3*size+1) ; buf[0]='\0'; }
	else if( size>=strlen(buf) ) buf=(char*)realloc ( buf, 3*size+1 ) ;

	license_init();
	if( letter_flag ) {
		if( !license_make_with_first( buf, size, modulo, 0 ) ) return_code=1 ;
		}
	else { if( !license_make( buf, size, modulo, 0 ) ) return_code=1 ; }
	
	if( return_code==0 ) {
		if( group_size>0 ) license_form( buf, sep, group_size ) ;
		printf( "%s\n", buf ) ;
		}
	else { fprintf( stderr, "Unable to create license\n" ); }
		
	return return_code ;
	}

#ifdef MAIN
int main( int argc, char *argv[], char *arge[] ) {
	return license_main( argc, argv, arge ) ;
	}
#endif
