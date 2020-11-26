#ifndef _DEVICEDEFS_H_
#define _DEVICEDEFS_H_

#ifdef __cplusplus
/* Assume byte packing throughout */
extern "C" {
#endif	/* __cplusplus */

#include <devioctl.h>

#define LOADER_DEVICE_STRING L"\\Device\\VT"
#define LOADER_SYMBOLIC_STRING L"\\DosDevices\\VT"

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif // !_DEVICEDEFS_H_
