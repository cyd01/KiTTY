#include "config.h"
#include <windows.h>
#include "assert.h"
#include "console.h"

typedef union {
  LPTSTR lpBuffer;
  LPTSTR lpValue;
} LPTSTR99;

VOID PrintConsole(LPCTSTR lpFormat, ...) {
  va_list lpValues;
  LPTSTR99 Message;
  DWORD dwCharacters;
  va_start(lpValues, lpFormat);
  Message.lpBuffer = (LPTSTR) &Message.lpValue;
  dwCharacters = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING, lpFormat, 0, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), Message.lpBuffer, 0, &lpValues);
  va_end(lpValues);
  assert(dwCharacters != 0);
  assert(WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), Message.lpValue, dwCharacters, &dwCharacters, NULL) != 0);
  LocalFree(Message.lpBuffer);
}

