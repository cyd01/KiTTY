#include "config.h"
#include <windows.h>
#include <wincrypt.h>
#include "md5assert.h"
#include "console.h"
#include "main.h"

typedef struct {
  BOOL bSuccess;
  TCHAR szDigest[33];
} CHKSUM;

static CHKSUM CheckIntegrity(VOID) {
#define BUF_SIZE (MAX_PATH * sizeof(TCHAR))
  CHKSUM chk = { TRUE, { 0 } };
  BYTE Buffer[BUF_SIZE];
  HCRYPTPROV hProvider;
  HCRYPTHASH hHash;
  HANDLE hFile;
  DWORD dwRead;
  UINT nCount = 0;
  BYTE chksum[16];
  BYTE digest[16];
  DWORD size = sizeof(digest);
  UINT j = 0;
  UINT n = 0;
  assert(GetModuleFileName(NULL, (LPTSTR) Buffer, MAX_PATH) != 0);
  assert(CryptAcquireContext(&hProvider, NULL, MS_DEF_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) != 0);
  assert(CryptCreateHash(hProvider, CALG_MD5, 0, 0, &hHash) != 0);
  hFile = CreateFile((LPTSTR) Buffer, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  assert(hFile != INVALID_HANDLE_VALUE);
  do {
    assert(ReadFile(hFile, Buffer, BUF_SIZE, &dwRead, NULL) != 0);
    while (nCount < 16) {
      chksum[nCount] = Buffer[0x2C + nCount];
      Buffer[0x2C + nCount++] = 0;
    }
    assert(CryptHashData(hHash, Buffer, dwRead, 0) != 0);
  } while (dwRead == BUF_SIZE);
  assert(CloseHandle(hFile) != 0);
  assert(CryptGetHashParam(hHash, HP_HASHVAL, digest, &size, 0) != 0);
  assert(CryptDestroyHash(hHash) != 0);
  while (n < 16) {
    if (digest[n] != chksum[n]) {
      chk.bSuccess = FALSE;
    }
    #define hex "0123456789ABCDEF"
      chk.szDigest[j++] = hex[(digest[n] & 0xF0) >> 4];
      chk.szDigest[j++] = hex[digest[n++] & 0x0F];
    #undef hex
  }
  return chk;
#undef BUF_SIZE
}


BOOL CheckMD5Integrity(void) {
	CHKSUM r = CheckIntegrity() ;
	return r.bSuccess ;
	}

/*

int main(void) {
  CHKSUM r = (PrintConsole(TEXT("Vérification de l'intégrité du fichier ... ")), CheckIntegrity());
  PrintConsole(TEXT("%1!s! (%2!s!)\n"), (r.bSuccess != 0) ? TEXT("succès") : TEXT("échec"), r.szDigest);
  return 0;
}

*/
