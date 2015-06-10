#ifndef __WINDOWS2LINUX_H__
#define __WINDOWS2LINUX_H__

/*
 * LINUX SPECIFIC DEFINITIONS
*/
//
// Data types conversions
//
#include <stdlib.h>
#include <string.h>
#include "basicDataTypeConversions.h"

#ifdef __cplusplus
namespace avxsynth {
#endif // __cplusplus
//
// purposefully define the following MSFT definitions
// to mean nothing (as they do not mean anything on Linux)
//
#define __stdcall
#define __cdecl
#define noreturn
#define __declspec(x)
#define STDAPI       extern "C" HRESULT
#define STDMETHODIMP HRESULT __stdcall
#define STDMETHODIMP_(x) x __stdcall

#define STDMETHOD(x)    virtual HRESULT x
#define STDMETHOD_(a, x) virtual a x

#ifndef TRUE
#define TRUE  true
#endif

#ifndef FALSE
#define FALSE false
#endif

#define S_OK                (0x00000000)
#define S_FALSE             (0x00000001)
#define E_NOINTERFACE       (0X80004002)
#define E_POINTER           (0x80004003)
#define E_FAIL              (0x80004005)
#define E_OUTOFMEMORY       (0x8007000E)

#define INVALID_HANDLE_VALUE    ((HANDLE)((LONG_PTR)-1))
#define FAILED(hr)              ((hr) & 0x80000000)
#define SUCCEEDED(hr)           (!FAILED(hr))


//
// Functions
//
#define MAKEDWORD(a,b,c,d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define MAKEWORD(a,b) (((a) << 8) | (b))

#define lstrlen                             strlen
#define lstrcpy                             strcpy
#define lstrcmpi                            strcasecmp
#define _stricmp                            strcasecmp
#define InterlockedIncrement(x)             __sync_fetch_and_add((x), 1)
#define InterlockedDecrement(x)             __sync_fetch_and_sub((x), 1)
// Windows uses (new, old) ordering but GCC has (old, new)
#define InterlockedCompareExchange(x,y,z)   __sync_val_compare_and_swap(x,z,y)

#define UInt32x32To64(a, b)                 ( (uint64_t) ( ((uint64_t)((uint32_t)(a))) * ((uint32_t)(b))  ) )
#define Int64ShrlMod32(a, b)                ( (uint64_t) ( (uint64_t)(a) >> (b) ) )
#define Int32x32To64(a, b)                  ((__int64)(((__int64)((long)(a))) * ((long)(b))))

#define MulDiv(nNumber, nNumerator, nDenominator)   (int32_t) (((int64_t) (nNumber) * (int64_t) (nNumerator) + (int64_t) ((nDenominator)/2)) / (int64_t) (nDenominator))

#ifdef __cplusplus
}; // namespace avxsynth
#endif // __cplusplus

#endif //  __WINDOWS2LINUX_H__
