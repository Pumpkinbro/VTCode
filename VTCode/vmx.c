#include <defs.h>

#include "vmx.h"

VMX_RESULT
NTAPI
__vmx_vmwrite_common(
    __in SIZE_T Field,
    __in ULONG64 Value
)
{
    VMX_RESULT Result = VMX_SUCCEED;
    ULONG64 ErrorCode = 0;

    switch ((Field >> ENCODE_SHIFT) & ENCODE_MASK) {
    case ENCODE_16:
    case ENCODE_32: {
        Value &= 0x00000000FFFFFFFF;

        Result = __vmx_vmwrite(Field, Value);

        break;
    }

    case ENCODE_64: {
#ifndef _WIN64
        Result = __vmx_vmwrite(Field, Value);

        if (VMX_SUCCEED == Result) {
            Result = __vmx_vmwrite(Field + 1, Value >> 32);
        }
#else
        Result = __vmx_vmwrite(Field, Value);
#endif // !_WIN64

        break;
    }

    case ENCODE_NATURAL: {
        Result = __vmx_vmwrite(Field, Value);

        break;
    }

    default: {
        Result = VMX_FAIL_INVALID;

        break;
    }
    }

    return Result;
}

VMX_RESULT
NTAPI
__vmx_vmread_common(
    __in SIZE_T Field,
    __out ULONG64 * Value
)
{
    VMX_RESULT Result = VMX_SUCCEED;
    ULONG64 ErrorCode = 0;

    switch ((Field >> ENCODE_SHIFT) & ENCODE_MASK) {
    case ENCODE_16:
    case ENCODE_32: {
        Result = __vmx_vmread(Field, Value);

        break;
    }

    case ENCODE_64: {
#ifndef _WIN64
        Result = __vmx_vmread(Field, Value);

        if (VMX_SUCCEED == Result) {
            Result = __vmx_vmread(Field + 1, (PULONG)Value + 1);
        }
#else
        Result = __vmx_vmread(Field, Value);
#endif // !_WIN64

        break;
    }

    case ENCODE_NATURAL: {
        Result = __vmx_vmread(Field, Value);

        break;
    }

    default: {
        Result = VMX_FAIL_INVALID;

        break;
    }
    }

    return Result;
}

VOID
NTAPI
__vmx_prepare_segment(
    __in PKDESCRIPTOR Descriptor,
    __in UINT16 Selector,
    __out PSEGMENT_DESCRIPTOR SegmentDescriptor
)
{
#ifndef _WIN64
    PKGDTENTRY GdtEntry = NULL;
#else
    PKGDTENTRY64 GdtEntry = NULL;
#endif // !_WIN64

    RtlZeroMemory(SegmentDescriptor, sizeof(SEGMENT_DESCRIPTOR));

    if ((0 == Selector) ||
        0 != (Selector & SELECTOR_TABLE_INDEX)) {
        SegmentDescriptor->Selector = 0;
        SegmentDescriptor->Limit = 0;
        SegmentDescriptor->Base = 0;
        SegmentDescriptor->AccessRights = 0x10000;
    }

#ifndef _WIN64
    GdtEntry =
        (PKGDTENTRY)((ULONG_PTR)Descriptor->Base + (Selector & ~RPL_MASK));

    SegmentDescriptor->Selector = Selector;
    SegmentDescriptor->Limit = __ops_segment_limit(Selector);
    SegmentDescriptor->BaseLow = GdtEntry->BaseLow;
    SegmentDescriptor->BaseMiddle = GdtEntry->HighWord.Bits.BaseMid;
    SegmentDescriptor->BaseHigh = GdtEntry->HighWord.Bits.BaseHi;
    SegmentDescriptor->AccessRights = __ops_segment_ar(Selector);
#else
    GdtEntry =
        (PKGDTENTRY64)((ULONG_PTR)Descriptor->Base + (Selector & ~RPL_MASK));

    SegmentDescriptor->Selector = Selector;
    SegmentDescriptor->Limit = __ops_segment_limit(Selector);
    SegmentDescriptor->BaseLow = GdtEntry->BaseLow;
    SegmentDescriptor->BaseMiddle = GdtEntry->Bits.BaseMiddle;
    SegmentDescriptor->BaseHigh = GdtEntry->Bits.BaseHigh;

    SegmentDescriptor->BaseUpper =
        0 == (GdtEntry->Bits.Type & 0x10) ? GdtEntry->BaseUpper : 0;

    SegmentDescriptor->AccessRights = __ops_segment_ar(Selector);
#endif // !_WIN64
}

NTSTATUS
NTAPI
__vmx_entry(
    __in PREGISTERS_FRAME Registers
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    VMX_RESULT Result = VMX_SUCCEED;
    ULONG Index = 0;
    ULONG64 ErrorCode = 0;
    SEGMENT_DESCRIPTOR SegmentDescriptor = { 0 };
    PCCB CurrentBlock = NULL;

    CurrentBlock = CONTAINING_RECORD(Registers, CCB, Registers);

    ReadCpuFeature(&CurrentBlock->Feature);

    if (0 == X86_FEATURE_VMX(&CurrentBlock->Feature)) {
        DbgPrint("[Daat] vmx not supported\n");

        return STATUS_UNSUCCESSFUL;
    }

    if (FC_LOCKED == (__ops_readmsr(IA32_FEATURE_CONTROL) & FC_LOCKED)) {
        if (FC_VMXON_INSMX != (__ops_readmsr(IA32_FEATURE_CONTROL) & FC_VMXON_INSMX)) {
            DbgPrint("[Daat] FC_VMXON_INSMX bit clear\n");
        }

        if (FC_VMXON_OUTSMX != (__ops_readmsr(IA32_FEATURE_CONTROL) & FC_VMXON_OUTSMX)) {
            DbgPrint("[Daat] FC_VMXON_OUTSMX bit clear\n");
        }
    }
    else {
        DbgPrint("[Daat] FC_LOCKED bit clear\n");

        return STATUS_UNSUCCESSFUL;
    }

    CurrentBlock->VmxInfo.BaseInfo = __ops_readmsr(IA32_VMX_BASIC);

    CurrentBlock->Region.VmsSupport.Identifier = CurrentBlock->VmxInfo.Identifier;
    CurrentBlock->Region.Vmcs.Identifier = CurrentBlock->VmxInfo.Identifier;

    CurrentBlock->VmsSupport = MmGetPhysicalAddress(&CurrentBlock->Region.VmsSupport);
    CurrentBlock->Vmcs = MmGetPhysicalAddress(&CurrentBlock->Region.Vmcs);
    CurrentBlock->Bitmap = MmGetPhysicalAddress(&CurrentBlock->Region.Bitmap);

    Registers->Cr0 = __ops_readcr(0);

    CurrentBlock->VmxInfo.Cr0.LowPart = CurrentBlock->Registers.Cr0;

    CurrentBlock->VmxInfo.Cr0Fixed.LowPart = __ops_readmsr(IA32_VMX_CR0_FIXED0);
    CurrentBlock->VmxInfo.Cr0Fixed.HighPart = __ops_readmsr(IA32_VMX_CR0_FIXED1);

    CurrentBlock->VmxInfo.Cr0.LowPart &= CurrentBlock->VmxInfo.Cr0Fixed.HighPart;;
    CurrentBlock->VmxInfo.Cr0.LowPart |= CurrentBlock->VmxInfo.Cr0Fixed.LowPart;

    DbgPrint(
        "[Daat] cr0 < %016x >\n",
        CurrentBlock->VmxInfo.Cr0.QuadPart);

    __ops_writecr(0, CurrentBlock->VmxInfo.Cr0.QuadPart);

    CurrentBlock->VmxInfo.Cr0ReadShadow.LowPart = CurrentBlock->Registers.Cr0;

    DbgPrint(
        "[Daat] cr0 read shadow < %016x >\n",
        CurrentBlock->VmxInfo.Cr0ReadShadow.QuadPart);

    CurrentBlock->VmxInfo.Cr0Mask.LowPart = CR0_PE | CR0_NE | CR0_PG;

    DbgPrint(
        "[Daat] cr0 mask < %016x >\n",
        CurrentBlock->VmxInfo.Cr0Mask.QuadPart);

    Registers->Cr4 = __ops_readcr(4);

    CurrentBlock->VmxInfo.Cr4.LowPart = CurrentBlock->Registers.Cr4;

    CurrentBlock->VmxInfo.Cr4Fixed.LowPart = __ops_readmsr(IA32_VMX_CR4_FIXED0);
    CurrentBlock->VmxInfo.Cr4Fixed.HighPart = __ops_readmsr(IA32_VMX_CR4_FIXED1);

    CurrentBlock->VmxInfo.Cr4.LowPart &= CurrentBlock->VmxInfo.Cr4Fixed.HighPart;;
    CurrentBlock->VmxInfo.Cr4.LowPart |= CurrentBlock->VmxInfo.Cr4Fixed.LowPart;

    DbgPrint(
        "[Daat] cr4 < %016x >\n",
        CurrentBlock->VmxInfo.Cr4.QuadPart);

    __ops_writecr(4, CurrentBlock->VmxInfo.Cr4.QuadPart);

    CurrentBlock->VmxInfo.Cr4ReadShadow.LowPart = CurrentBlock->Registers.Cr4;

    DbgPrint(
        "[Daat] cr4 read shadow < %016x >\n",
        CurrentBlock->VmxInfo.Cr4ReadShadow.QuadPart);

    CurrentBlock->VmxInfo.Cr4Mask.LowPart = CR4_VMXE;

    DbgPrint(
        "[Daat] cr4 mask < %016x >\n",
        CurrentBlock->VmxInfo.Cr4Mask.QuadPart);

    Result = __vmx_on(&CurrentBlock->VmsSupport);

    if (VMX_SUCCEED != Result) {
        DbgPrint("[Daat] vmxon failed\n");

        return STATUS_UNSUCCESSFUL;
    }

    Result = __vmx_vmclear(&CurrentBlock->Vmcs);

    if (VMX_SUCCEED != Result) {
        DbgPrint("[Daat] vmclear failed\n");

        __vmx_off();

        return STATUS_UNSUCCESSFUL;
    }

    Result = __vmx_vmptrld(&CurrentBlock->Vmcs);

    if (VMX_SUCCEED != Result) {
        DbgPrint("[Daat] vmptrld failed\n");

        __vmx_off();

        return STATUS_UNSUCCESSFUL;
    }

    __ops_sgdt(&CurrentBlock->Registers.Gdtr.Limit);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.SegEs,
        &SegmentDescriptor);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_ES_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_ES_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_ES_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_ES_AR, SegmentDescriptor.AccessRights);
    __vmx_vmwrite_common(HOST_ES_SELECTOR, SegmentDescriptor.Selector & ~RPL_MASK);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.SegCs,
        &SegmentDescriptor);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_CS_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_CS_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_CS_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_CS_AR, SegmentDescriptor.AccessRights);
    __vmx_vmwrite_common(HOST_CS_SELECTOR, SegmentDescriptor.Selector & ~RPL_MASK);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.SegSs,
        &SegmentDescriptor);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_SS_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_SS_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_SS_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_SS_AR, SegmentDescriptor.AccessRights);
    __vmx_vmwrite_common(HOST_SS_SELECTOR, SegmentDescriptor.Selector & ~RPL_MASK);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.SegDs,
        &SegmentDescriptor);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_DS_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_DS_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_DS_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_DS_AR, SegmentDescriptor.AccessRights);
    __vmx_vmwrite_common(HOST_DS_SELECTOR, SegmentDescriptor.Selector & ~RPL_MASK);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.SegFs,
        &SegmentDescriptor);

    SegmentDescriptor.Base = __ops_readmsr(IA32_FS_BASE);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_FS_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_FS_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_FS_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_FS_AR, SegmentDescriptor.AccessRights);
    __vmx_vmwrite_common(HOST_FS_SELECTOR, SegmentDescriptor.Selector & ~RPL_MASK);
    __vmx_vmwrite_common(HOST_FS_BASE, SegmentDescriptor.Base);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.SegGs,
        &SegmentDescriptor);

    SegmentDescriptor.Base = __ops_readmsr(IA32_GS_BASE);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_GS_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_GS_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_GS_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_GS_AR, SegmentDescriptor.AccessRights);
    __vmx_vmwrite_common(HOST_GS_SELECTOR, SegmentDescriptor.Selector & ~RPL_MASK);
    __vmx_vmwrite_common(HOST_GS_BASE, SegmentDescriptor.Base);

    __ops_sldt(&CurrentBlock->Registers.Ldtr);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.Ldtr,
        &SegmentDescriptor);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_LDTR_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_LDTR_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_LDTR_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_LDTR_AR, SegmentDescriptor.AccessRights);

    __ops_str(&CurrentBlock->Registers.Tr);

    __vmx_prepare_segment(
        &CurrentBlock->Registers.Gdtr,
        CurrentBlock->Registers.Tr,
        &SegmentDescriptor);

    DbgPrint(
        "[Daat] segment < %02x | %p | %p | %04x >\n",
        SegmentDescriptor.Selector,
        SegmentDescriptor.Limit,
        SegmentDescriptor.Base,
        SegmentDescriptor.AccessRights);

    __vmx_vmwrite_common(GUEST_TR_SELECTOR, SegmentDescriptor.Selector);
    __vmx_vmwrite_common(GUEST_TR_BASE, SegmentDescriptor.Base);
    __vmx_vmwrite_common(GUEST_TR_LIMIT, SegmentDescriptor.Limit);
    __vmx_vmwrite_common(GUEST_TR_AR, SegmentDescriptor.AccessRights);
    __vmx_vmwrite_common(HOST_TR_SELECTOR, SegmentDescriptor.Selector & ~RPL_MASK);
    __vmx_vmwrite_common(HOST_TR_BASE, SegmentDescriptor.Base);

    DbgPrint(
        "[Daat] gdtr < %04x | %p >\n",
        CurrentBlock->Registers.Gdtr.Limit,
        CurrentBlock->Registers.Gdtr.Base);

    __vmx_vmwrite_common(GUEST_GDTR_LIMIT, CurrentBlock->Registers.Gdtr.Limit);
    __vmx_vmwrite_common(GUEST_GDTR_BASE, (ULONG64)CurrentBlock->Registers.Gdtr.Base);
    __vmx_vmwrite_common(HOST_GDTR_BASE, (ULONG64)CurrentBlock->Registers.Gdtr.Base);

    __ops_sidt(&CurrentBlock->Registers.Idtr.Limit);

    DbgPrint(
        "[Daat] idtr < %04x | %p >\n",
        CurrentBlock->Registers.Idtr.Limit,
        CurrentBlock->Registers.Idtr.Base);

    __vmx_vmwrite_common(GUEST_IDTR_LIMIT, CurrentBlock->Registers.Idtr.Limit);
    __vmx_vmwrite_common(GUEST_IDTR_BASE, (ULONG64)CurrentBlock->Registers.Idtr.Base);
    __vmx_vmwrite_common(HOST_IDTR_BASE, (ULONG64)CurrentBlock->Registers.Idtr.Base);

    Registers->Cr3 = __ops_readcr(3);

    __vmx_vmwrite_common(GUEST_CR0, CurrentBlock->VmxInfo.Cr0.QuadPart);
    __vmx_vmwrite_common(GUEST_CR3, CurrentBlock->Registers.Cr3);
    __vmx_vmwrite_common(GUEST_CR4, CurrentBlock->VmxInfo.Cr4.QuadPart);

    CurrentBlock->Registers.Dr7 = __ops_readdr(7);

    __vmx_vmwrite_common(GUEST_DR7, CurrentBlock->Registers.Dr7);
    __vmx_vmwrite_common(GUEST_RFLAGS, CurrentBlock->Registers.EFlags);

    __vmx_vmwrite_common(GUEST_RSP, (ULONG64)CurrentBlock->Registers.StackPointer);
    __vmx_vmwrite_common(GUEST_RIP, (ULONG64)CurrentBlock->Registers.ProgramCounter);

    __vmx_vmwrite_common(GUEST_DEBUGCTL, __ops_readmsr(IA32_DEBUGCTL));
    __vmx_vmwrite_common(GUEST_SYSENTER_CS, __ops_readmsr(IA32_SYSENTER_CS));
    __vmx_vmwrite_common(GUEST_SYSENTER_ESP, __ops_readmsr(IA32_SYSENTER_ESP));
    __vmx_vmwrite_common(GUEST_SYSENTER_EIP, __ops_readmsr(IA32_SYSENTER_EIP));

    __vmx_vmwrite_common(GUEST_INTERRUPTIBILITY, 0);
    __vmx_vmwrite_common(GUEST_ACTIVITY_STATE, 0);
    __vmx_vmwrite_common(GUEST_VMCS_LINK_PTR, VMCS_NONE);

    __vmx_vmwrite_common(HOST_CR0, CurrentBlock->VmxInfo.Cr0.QuadPart);
    __vmx_vmwrite_common(HOST_CR3, CurrentBlock->Registers.Cr3);
    __vmx_vmwrite_common(HOST_CR4, CurrentBlock->VmxInfo.Cr4.QuadPart);

    __vmx_vmwrite_common(HOST_RSP, (ULONG64)&CurrentBlock->Registers);
    __vmx_vmwrite_common(HOST_RIP, (ULONG64)__vmx_vmentry);

    __vmx_vmwrite_common(HOST_SYSENTER_CS, __ops_readmsr(IA32_SYSENTER_CS));
    __vmx_vmwrite_common(HOST_SYSENTER_ESP, __ops_readmsr(IA32_SYSENTER_ESP));
    __vmx_vmwrite_common(HOST_SYSENTER_EIP, __ops_readmsr(IA32_SYSENTER_EIP));

    __vmx_vmwrite_common(VMX_CR0_MASK, CurrentBlock->VmxInfo.Cr0Mask.QuadPart);
    __vmx_vmwrite_common(VMX_CR0_READ_SHADOW, CurrentBlock->VmxInfo.Cr0ReadShadow.QuadPart);

    __vmx_vmwrite_common(VMX_CR4_MASK, CurrentBlock->VmxInfo.Cr4Mask.QuadPart);
    __vmx_vmwrite_common(VMX_CR4_READ_SHADOW, CurrentBlock->VmxInfo.Cr4ReadShadow.QuadPart);

    // read 0x00000000 - 0x00001FFF 0 ~ 1023
    // read 0xC0000000 - 0xC0001FFF 1024 ~ 2047
#define SetReadBitmap(map, bit) \
            *((PCHAR)(map) + \
                (((bit) & 0xC0000000) ? 0x400 : 0) + \
                    (((bit) & 0x1FFF) >> 3)) |= 1 << ((bit) & 7);

    // write 0x00000000 - 0x00001FFF 2048 ~ 3071
    // write 0xC0000000 - 0xC0001FFF 3072 ~ 4095
#define SetWriteBitmap(map, bit) \
            *((PCHAR)(map) + \
                (((bit) & 0xC0000000) ? 0xc00 : 0x800) + \
                    (((bit) & 0x1FFF) >> 3)) |= 1 << ((bit) & 7);

#define SetBitmap(map, bit) { \
            SetReadBitmap(map, bit); \
            SetWriteBitmap(map, bit); \
            }

    SetBitmap(CurrentBlock->Region.Bitmap, IA32_DEBUGCTL);
    SetBitmap(CurrentBlock->Region.Bitmap, IA32_SYSENTER_CS);
    SetBitmap(CurrentBlock->Region.Bitmap, IA32_SYSENTER_ESP);
    SetBitmap(CurrentBlock->Region.Bitmap, IA32_SYSENTER_EIP);
    SetBitmap(CurrentBlock->Region.Bitmap, IA32_FS_BASE);
    SetBitmap(CurrentBlock->Region.Bitmap, IA32_GS_BASE);
    SetBitmap(CurrentBlock->Region.Bitmap, IA32_FEATURE_CONTROL);

    __vmx_vmwrite_common(VMX_MSR_BITMAP, CurrentBlock->Bitmap.QuadPart);

    // CurrentBlock->ExceptionBitmap.LowPart |= 1UL << VECTOR_NMI;
    // CurrentBlock->ExceptionBitmap.LowPart |= 1UL << VECTOR_DB;
    // CurrentBlock->ExceptionBitmap.LowPart |= 1UL << VECTOR_BP;

    __vmx_vmwrite_common(VMX_EXCEPTION_BITMAP, CurrentBlock->ExceptionBitmap.QuadPart);

    __vmx_vmwrite_common(VMX_TSC_OFFSET, 0);

    if (CurrentBlock->VmxInfo.BaseInfo & (1ULL << 55)) {
        CurrentBlock->VmxInfo.PinFixed.QuadPart = __ops_readmsr(IA32_VMX_TRUE_PINBASED_CTLS);
        CurrentBlock->VmxInfo.PrimaryFixed.QuadPart = __ops_readmsr(IA32_VMX_TRUE_PROCBASED_CTLS);
        CurrentBlock->VmxInfo.ExitFixed.QuadPart = __ops_readmsr(IA32_VMX_TRUE_EXIT_CTLS);
        CurrentBlock->VmxInfo.EntryFixed.QuadPart = __ops_readmsr(IA32_VMX_TRUE_ENTRY_CTLS);

        CurrentBlock->VmxInfo.PinFixed.LowPart |=
            __ops_readmsr(IA32_VMX_PINBASED_CTLS) & ~PIN_CONTROLS_DEFINED;

        CurrentBlock->VmxInfo.PrimaryFixed.LowPart |=
            __ops_readmsr(IA32_VMX_PROCBASED_CTLS) & ~PRIMARY_CONTROLS_DEFINED;

        CurrentBlock->VmxInfo.ExitFixed.LowPart |=
            __ops_readmsr(IA32_VMX_EXIT_CTLS) & ~EXIT_CONTROLS_DEFINED;

        CurrentBlock->VmxInfo.EntryFixed.LowPart |=
            __ops_readmsr(IA32_VMX_ENTRY_CTLS) & ~ENTRY_CONTROLS_DEFINED;
    }
    else {
        CurrentBlock->VmxInfo.PinFixed.QuadPart = __ops_readmsr(IA32_VMX_PINBASED_CTLS);
        CurrentBlock->VmxInfo.PrimaryFixed.QuadPart = __ops_readmsr(IA32_VMX_PROCBASED_CTLS);
        CurrentBlock->VmxInfo.ExitFixed.QuadPart = __ops_readmsr(IA32_VMX_EXIT_CTLS);
        CurrentBlock->VmxInfo.EntryFixed.QuadPart = __ops_readmsr(IA32_VMX_ENTRY_CTLS);
    }

    DbgPrint(
        "[Daat] pin fixed control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.PinFixed.HighPart,
        CurrentBlock->VmxInfo.PinFixed.LowPart);

    DbgPrint(
        "[Daat] primary fixed control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.PrimaryFixed.HighPart,
        CurrentBlock->VmxInfo.PrimaryFixed.LowPart);

    DbgPrint(
        "[Daat] exit fixed control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.ExitFixed.HighPart,
        CurrentBlock->VmxInfo.ExitFixed.LowPart);

    DbgPrint(
        "[Daat] entry fixed control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.EntryFixed.HighPart,
        CurrentBlock->VmxInfo.EntryFixed.LowPart);

    if (SECONDARY_CONTROLS ==
        (CurrentBlock->VmxInfo.PrimaryFixed.HighPart & SECONDARY_CONTROLS)) {
        CurrentBlock->VmxInfo.SecondaryFixed.QuadPart = __ops_readmsr(IA32_VMX_SECONDARY_CTLS);

        DbgPrint(
            "[Daat] secondary fixed control < %08x | %08x >\n",
            CurrentBlock->VmxInfo.SecondaryFixed.HighPart,
            CurrentBlock->VmxInfo.SecondaryFixed.LowPart);
    }

    CurrentBlock->VmxInfo.Pin.LowPart &= CurrentBlock->VmxInfo.PinFixed.HighPart;
    CurrentBlock->VmxInfo.Pin.LowPart |= CurrentBlock->VmxInfo.PinFixed.LowPart;

    __vmx_vmwrite_common(VMX_PIN_CONTROLS, CurrentBlock->VmxInfo.Pin.QuadPart);

    DbgPrint(
        "[Daat] pin control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.Pin.HighPart,
        CurrentBlock->VmxInfo.Pin.LowPart);

    if (0 != X86_FEATURE_RDTSCP(&CurrentBlock->Feature)) {
        CurrentBlock->VmxInfo.Secondary.LowPart |= ENABLE_RDTSCP;
    }

    CurrentBlock->VmxInfo.Secondary.LowPart &= CurrentBlock->VmxInfo.SecondaryFixed.HighPart;
    CurrentBlock->VmxInfo.Secondary.LowPart |= CurrentBlock->VmxInfo.SecondaryFixed.LowPart;

    __vmx_vmwrite_common(VMX_SECONDARY_PROCESSOR_CONTROLS, CurrentBlock->VmxInfo.Secondary.QuadPart);

    DbgPrint(
        "[Daat] secondary control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.Secondary.HighPart,
        CurrentBlock->VmxInfo.Secondary.LowPart);

    CurrentBlock->VmxInfo.Primary.LowPart |= DR_EXITING;
    CurrentBlock->VmxInfo.Primary.LowPart |= MSR_BITMAP_ACTIVE;
    CurrentBlock->VmxInfo.Primary.LowPart |= SECONDARY_CONTROLS;

    CurrentBlock->VmxInfo.Primary.LowPart &= CurrentBlock->VmxInfo.PrimaryFixed.HighPart;
    CurrentBlock->VmxInfo.Primary.LowPart |= CurrentBlock->VmxInfo.PrimaryFixed.LowPart;

    __vmx_vmwrite_common(VMX_PRIMARY_PROCESSOR_CONTROLS, CurrentBlock->VmxInfo.Primary.QuadPart);

    DbgPrint(
        "[Daat] primary control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.Primary.HighPart,
        CurrentBlock->VmxInfo.Primary.LowPart);

    CurrentBlock->VmxInfo.Exit.LowPart |= ENTRY_CONTROL_LOAD_DEBUG_CONTROLS;

#ifdef _WIN64
    CurrentBlock->VmxInfo.Exit.LowPart |= EXIT_CONTROL_HOST_ADDR_SPACE_SIZE;
#endif // _WIN64

    CurrentBlock->VmxInfo.Exit.LowPart &= CurrentBlock->VmxInfo.ExitFixed.HighPart;
    CurrentBlock->VmxInfo.Exit.LowPart |= CurrentBlock->VmxInfo.ExitFixed.LowPart;

    __vmx_vmwrite_common(VMX_EXIT_CONTROLS, CurrentBlock->VmxInfo.Exit.QuadPart);

    DbgPrint(
        "[Daat] exit control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.Exit.HighPart,
        CurrentBlock->VmxInfo.Exit.LowPart);

    CurrentBlock->VmxInfo.Entry.LowPart |= EXIT_CONTROL_SAVE_DEBUG_CONTROLS;

#ifdef _WIN64
    CurrentBlock->VmxInfo.Entry.LowPart |= ENTRY_CONTROL_LONG_MODE_GUEST;
#endif // _WIN64

    CurrentBlock->VmxInfo.Entry.LowPart &= CurrentBlock->VmxInfo.EntryFixed.HighPart;
    CurrentBlock->VmxInfo.Entry.LowPart |= CurrentBlock->VmxInfo.EntryFixed.LowPart;

    __vmx_vmwrite_common(VMX_ENTRY_CONTROLS, CurrentBlock->VmxInfo.Entry.QuadPart);

    DbgPrint(
        "[Daat] entry control < %08x | %08x >\n",
        CurrentBlock->VmxInfo.Entry.HighPart,
        CurrentBlock->VmxInfo.Entry.LowPart);

    Result = __vmx_vmlaunch();

    if (VMX_SUCCEED != Result) {
        if (VMX_FAIL_INVALID == Result) {
            __vmx_vmread_common(VMX_INSTRUCTION_ERROR_CODE, &ErrorCode);
        }

        DbgPrint(
            "[Daat] vmlaunch failed < %p >\n",
            ErrorCode);

        __vmx_off();

        return STATUS_UNSUCCESSFUL;
    }

    return Status;
}

VOID
NTAPI
VmxStartProcessors(
    __in struct _KDPC * Dpc,
    __in PVOID DeferredContext,
    __in PVOID SystemArgument1,
    __in PVOID SystemArgument2
)
{
    PCCB DpcNotify = NULL;
    PCCB CurrentBlock = NULL;

    CurrentBlock = DeferredContext;

#ifndef PUBLIC
    DbgPrint(
        "[Daat] < %p : %p > current processor\n",
        KeGetCurrentProcessorNumber(),
        CurrentBlock);
#endif // !PUBLIC

    __vmx_start(&CurrentBlock->Registers);

    DpcNotify = SystemArgument1;

    KeSetEvent(DpcNotify, LOW_PRIORITY, FALSE);
}

VOID
NTAPI
VmxStartAllProcessors(
    __in PKEVENT Notify
)
{
    UNICODE_STRING RoutineString = { 0 };
    PCCHAR NumberProcessors = NULL;
    CHAR Index = 0;
    KEVENT DpcNotify = { 0 };
    KDPC Dpc = { 0 };
    PCCB ControlBlock = NULL;
    PCCB CurrentBlock = NULL;

    RtlInitUnicodeString(&RoutineString, L"KeNumberProcessors");

    NumberProcessors = MmGetSystemRoutineAddress(&RoutineString);

    ControlBlock = ExAllocatePool(
        NonPagedPool,
        *NumberProcessors * sizeof(CCB));

    if (NULL != ControlBlock) {
        RtlZeroMemory(
            ControlBlock,
            *NumberProcessors * sizeof(CCB));

        for (Index = 0;
            Index < *NumberProcessors;
            Index++) {
            CurrentBlock = ControlBlock + Index;

            KeInitializeEvent(
                &DpcNotify,
                SynchronizationEvent,
                FALSE);

            KeInitializeDpc(
                &Dpc,
                VmxStartProcessors,
                CurrentBlock);

            KeSetTargetProcessorDpc(&Dpc, Index);
            KeSetImportanceDpc(&Dpc, HighImportance);

            if (FALSE != KeInsertQueueDpc(
                &Dpc,
                &DpcNotify,
                NULL)) {
                KeWaitForSingleObject(
                    &DpcNotify,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL);
            }
        }
    }

    KeSetEvent(Notify, LOW_PRIORITY, FALSE);
}
