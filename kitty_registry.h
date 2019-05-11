#ifndef KITTY_REGISTRY
#define KITTY_REGISTRY

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

#ifndef MAX_KEY_LENGTH 
#define MAX_KEY_LENGTH 255
#endif
#ifndef MAX_VALUE_NAME
#define MAX_VALUE_NAME 16383
#endif

char * GetValueData(HKEY hkTopKey, char * lpSubKey, const char * lpValueName, char * rValue) ;


// Teste l'existance d'une clé
int RegTestKey( HKEY hMainKey, LPCTSTR lpSubKey ) ;

// Retourne le nombre de sous-keys
int RegCountKey( HKEY hMainKey, LPCTSTR lpSubKey ) ;

// Teste l'existance d'une clé ou bien d'une valeur et la crée sinon
void RegTestOrCreate( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR name, LPCTSTR value ) ;
	
// Test l'existance d'une clé ou bien d'une valeur DWORD et la crée sinon
void RegTestOrCreateDWORD( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR name, DWORD value ) ;

// Initialise toutes les sessions avec une valeur (si oldvalue==NULL) ou uniquement celles qui ont la valeur oldvalue
void RegUpdateAllSessions( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR name, LPCTSTR oldvalue, LPCTSTR value  ) ;

// Exporte toute une cle de registre
void QuerySubKey( HKEY hMainKey, LPCTSTR lpSubKey, FILE * fp_out, char * text  ) ;

// Détruit une valeur de clé de registre 
BOOL RegDelValue (HKEY hKeyRoot, LPTSTR lpSubKey, LPTSTR lpValue ) ;

// Detruit une clé de registre et ses sous-clé
BOOL RegDelTree (HKEY hKeyRoot, LPCTSTR lpSubKey) ;

// Copie une clé de registre vers une autre
void RegCopyTree( HKEY hMainKey, LPCTSTR lpSubKey, LPCTSTR lpDestKey ) ;

// Nettoie la clé de PuTTY pour enlever les clés et valeurs spécifique à KiTTY
BOOL RegCleanPuTTY( void ) ;

// Creation du SSH Handler
void CreateSSHHandler() ;

// Creation de l'association de fichiers *.ktx
void CreateFileAssoc() ;

// Vérifie l'existance de la clé de KiTTY sinon la copie depuis PuTTY
void TestRegKeyOrCopyFromPuTTY( HKEY hMainKey, char * KeyName ) ;

void InitRegistryAllSessions( HKEY hMainKey, LPCTSTR lpSubKey, char * SubKeyName, char * filename, char * text ) ;

// Permet d'initialiser toutes les sessions avec des valeurs contenu dans un fichier kitty.ses.updt
void InitAllSessions( HKEY hMainKey, LPCTSTR lpSubKey, char * SubKeyName, char * filename ) ;
	
#endif
