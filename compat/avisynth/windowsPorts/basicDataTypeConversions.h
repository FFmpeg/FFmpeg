#ifndef __DATA_TYPE_CONVERSIONS_H__
#define __DATA_TYPE_CONVERSIONS_H__

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
namespace avxsynth {
#endif // __cplusplus

typedef int64_t __int64;
typedef int32_t __int32;
#ifdef __cplusplus
typedef bool BOOL;
#else
typedef uint32_t BOOL;
#endif // __cplusplus
typedef void* HMODULE;
typedef void* LPVOID;
typedef void* PVOID;
typedef PVOID HANDLE;
typedef HANDLE HWND;
typedef HANDLE HINSTANCE;
typedef void* HDC;
typedef void* HBITMAP;
typedef void* HICON;
typedef void* HFONT;
typedef void* HGDIOBJ;
typedef void* HBRUSH;
typedef void* HMMIO;
typedef void* HACMSTREAM;
typedef void* HACMDRIVER;
typedef void* HIC;
typedef void* HACMOBJ;
typedef HACMSTREAM* LPHACMSTREAM;
typedef void* HACMDRIVERID;
typedef void* LPHACMDRIVER;
typedef unsigned char BYTE;
typedef BYTE* LPBYTE;
typedef char TCHAR;
typedef TCHAR* LPTSTR;
typedef const TCHAR* LPCTSTR;
typedef char* LPSTR;
typedef LPSTR LPOLESTR;
typedef const char* LPCSTR;
typedef LPCSTR LPCOLESTR;
typedef wchar_t WCHAR;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef UINT MMRESULT;
typedef uint32_t DWORD;
typedef DWORD COLORREF;
typedef DWORD FOURCC;
typedef DWORD HRESULT;
typedef DWORD* LPDWORD;
typedef DWORD* DWORD_PTR;
typedef int32_t LONG;
typedef int32_t* LONG_PTR;
typedef LONG_PTR LRESULT;
typedef uint32_t ULONG;
typedef uint32_t* ULONG_PTR;
//typedef __int64_t intptr_t;
typedef uint64_t _fsize_t;


//
// Structures
//

typedef struct _GUID {
  DWORD Data1;
  WORD  Data2;
  WORD  Data3;
  BYTE  Data4[8];
} GUID;

typedef GUID REFIID;
typedef GUID CLSID;
typedef CLSID* LPCLSID;
typedef GUID IID;

#ifdef __cplusplus
}; // namespace avxsynth
#endif // __cplusplus
#endif //  __DATA_TYPE_CONVERSIONS_H__
