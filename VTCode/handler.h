#ifndef _HANDLER_H_
#define _HANDLER_H_

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

    typedef struct _CPUINFO {
        ULONG Eax;
        ULONG Ebx;
        ULONG Ecx;
        ULONG Edx;
    } CPUINFO, *PCPUINFO;

    typedef struct _CCB * PCCB;

    VOID
        NTAPI
        __ops_cpuid(
            __in LONG Function,
            __in LONG SubFunction,
            __out PCPUINFO CpuInfo
        );

    VOID
        NTAPI
        __ops_invd(
            VOID
        );

    ULONG64
        NTAPI
        __ops_rdtsc(
            VOID
        );

    ULONG64
        NTAPI
        __ops_rdtscp(
            __out PULONG Aux
        );

    VOID
        NTAPI
        __ops_xsetbv(
            __in ULONG Xcr,
            __in ULONG64 Value
        );

    VOID
        NTAPI
        __inject_exception(
            __inout PCCB Block,
            __in ULONG64 Vector,
            __in_opt ULONG64 ErrorCode,
            __in ULONG64 Type
        );

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif // !_HANDLER_H_
