#ifndef _LOG_H_
#define _LOG_H_

#ifdef __cplusplus
/* Assume byte packing throughout */
extern "C" {
#endif	/* __cplusplus */

#ifndef TRACE
#ifndef PUBLIC
#ifndef NTOS_KERNEL_RUNTIME
#define TRACE(exp) \
            (((NTSTATUS)exp) >= 0) ? \
                TRUE : \
                (vDbgPrint( \
                    TEXT("%hs[%d] %hs failed < %08x >\n"), \
                    __FILE__, \
                    __LINE__, \
                    __FUNCDNAME__, \
                    exp), FALSE)

    VOID
        CDECL
        vDbgPrint(
            __in PCTSTR Format,
            ...
        );
#else
#define TRACE(exp) \
            (((NTSTATUS)exp) >= 0) ? \
                TRUE : \
                (DbgPrint( \
                    "%hs[%d] %hs failed < %08x >\n", \
                    __FILE__, \
                    __LINE__, \
                    __FUNCDNAME__, \
                    exp), FALSE)
#endif // !NTOS_KERNEL_RUNTIME
#else
#define TRACE(exp) (((NTSTATUS)exp) >= 0)
#endif // !PUBLIC
#endif // !TRACE

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif // !_LOG_H_
