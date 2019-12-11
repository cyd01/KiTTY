#include "config.h"
#include <windows.h>
#include "md5assert.h"

#ifndef NDEBUG

  typedef union {
    LPTSTR lpBuffer;
    LPTSTR lpValue;
  } LPTSTR99;

  static LPTSTR _format(LPCTSTR lpFormat, ...) {
    va_list lpValues;
    LPTSTR99 Message;
    DWORD dwCharacters;
    va_start(lpValues, lpFormat);
    Message.lpBuffer = (LPTSTR) &Message.lpValue;
    dwCharacters = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING, lpFormat, 0, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), Message.lpBuffer, 0, &lpValues);
    va_end(lpValues);
    if (dwCharacters != 0) {
      return Message.lpValue;
    } else {
      MessageBoxEx(NULL, TEXT("Impossible de formater le message d'erreur."), TEXT(__FILE__) TEXT(":") TEXT(str(__LINE__)), MB_TOPMOST | MB_TASKMODAL | MB_ICONERROR | MB_OK, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
      ExitProcess(0);
    }
  }

  VOID NORETURN _assert(LPCTSTR lpFile, UINT nLine, LPCTSTR lpLine) {
    DWORD dwError;
    LPTSTR99 Error;
    dwError = GetLastError();
    Error.lpBuffer = (LPTSTR) &Error.lpValue;
    if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), Error.lpBuffer, 0, NULL) != 0) {
      LPTSTR lpCaption = _format(TEXT("%1!s!:%2!u!"), lpFile, nLine);
      LPTSTR lpMessage = _format(TEXT("Assertion non vérifiée :%n¤ %1!s!%n%nCode de retour :%n¤ %2!u!%n%nDescription de l'erreur :%n¤ %3!s!"), lpLine, dwError, Error.lpValue);
      MessageBoxEx(NULL, lpMessage, lpCaption, MB_TOPMOST | MB_TASKMODAL | MB_ICONERROR | MB_OK, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
      LocalFree(lpMessage);
      LocalFree(lpCaption);
      LocalFree(Error.lpBuffer);
    } else {
      MessageBoxEx(NULL, TEXT("Impossible de formater le message d'erreur."), TEXT(__FILE__) TEXT(":") TEXT(str(__LINE__)), MB_TOPMOST | MB_TASKMODAL | MB_ICONERROR | MB_OK, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
    }
    ExitProcess(0);
  }

#endif

