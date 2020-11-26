#include <defs.h>

#include "log.h"

VOID
CDECL
vDbgPrint(
    __in PCTSTR Format,
    ...
)
{
    PTSTR Buffer = NULL;
    SIZE_T BufferSize = 0;
    va_list ArgvList = NULL;

    va_start(
        ArgvList,
        Format);

    BufferSize = _vsctprintf(
        Format,
        ArgvList);

    if (-1 != BufferSize) {
        BufferSize += sizeof(TCHAR);

        Buffer = RtlAllocateHeap(
            RtlProcessHeap(),
            0,
            BufferSize * sizeof(TCHAR));

        if (NULL != Buffer) {
            if (-1 != _vsntprintf(
                Buffer,
                BufferSize,
                Format,
                ArgvList)) {
                OutputDebugString(Buffer);
            }

            RtlFreeHeap(
                RtlProcessHeap(),
                0,
                Buffer);
        }
    }
}
