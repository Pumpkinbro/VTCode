#ifndef _DEFS_H_
#define _DEFS_H_

#ifdef __cplusplus
/* Assume byte packing throughout */
extern "C" {
#endif	/* __cplusplus */

#ifndef PUBLIC
    // #define PUBLIC
#endif // !PUBLIC

#ifndef NTOS_KERNEL_RUNTIME
#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>

#include <windows.h>
#include <windowsx.h>
#else
#include <ntos.h>
#include <zwapi.h>
#endif // !NTOS_KERNEL_RUNTIME

#ifdef _WIN64
#include <wow64t.h>
#include <wow64.h>
#endif // _WIN64

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>

#include <arccodes.h>

#include <log.h>

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif // !_DEFS_H_
