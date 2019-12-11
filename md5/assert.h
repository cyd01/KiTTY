#ifndef __ASSERT_H__
  #define __ASSERT_H__

  #define _str(x) #x
  #define str(x) _str(x)

  #ifndef NDEBUG
    extern VOID NORETURN _assert(LPCTSTR, UINT, LPCTSTR);
    #define assert(x) ((x) ? SetLastErrorEx(0, SLE_WARNING) : _assert(TEXT(__FILE__), __LINE__, TEXT(#x)))
  #else
    #define assert(x) ((void) (x))
  #endif

#endif

