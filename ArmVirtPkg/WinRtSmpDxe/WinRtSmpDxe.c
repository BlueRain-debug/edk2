/** @file
  Publish ACPI parking mailboxes and bring SEC secondaries to their wait loop.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <IndustryStandard/Acpi65.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Library/ArmLib.h>
#include <Library/ArmGicLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <WinRtSmp.h>

extern UINT8  WinRtParkingStart[];
extern UINT8  WinRtParkingEnd[];

STATIC
VOID
EFIAPI
PrepareWinRtParking (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_ACPI_SDT_PROTOCOL        *Sdt;
  EFI_ACPI_TABLE_PROTOCOL      *Acpi;
  EFI_ACPI_SDT_HEADER          *Header;
  EFI_ACPI_TABLE_VERSION       Version;
  EFI_ACPI_DESCRIPTION_HEADER  *Original;
  EFI_ACPI_DESCRIPTION_HEADER  *Copy;
  EFI_ACPI_6_5_GIC_STRUCTURE   *Gic;
  EFI_STATUS                   Status;
  EFI_PHYSICAL_ADDRESS         Pages;
  UINTN                        Index;
  UINTN                        OldKey;
  UINTN                        NewKey;
  UINTN                        Offset;
  UINTN                        Count;
  UINTN                        Cpu;
  UINTN                        Mask;
  UINTN                        CodeSize;
  UINTN                        Retry;
  UINTN                        Bsp;
  UINT8                        *Page;
  volatile UINT32              *Release;
  volatile UINT32              *Ready;

  gBS->CloseEvent (Event);
  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&Sdt);
  if (EFI_ERROR (Status)) {
    return;
  }
  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&Acpi);
  if (EFI_ERROR (Status)) {
    return;
  }
  for (Index = 0; ; Index++) {
    Status = Sdt->GetAcpiTable (Index, &Header, &Version, &OldKey);
    if (EFI_ERROR (Status)) {
      return;
    }
    if (Header->Signature == SIGNATURE_32 ('A', 'P', 'I', 'C')) {
      break;
    }
  }
  if (Header->Length < sizeof (EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER)) {
    DEBUG ((DEBUG_ERROR, "WinRT SMP: truncated MADT\n"));
    return;
  }
  Original = AllocateCopyPool (Header->Length, Header);
  if (Original == NULL) {
    return;
  }
  Copy = AllocateCopyPool (Header->Length, Header);
  if (Copy == NULL) {
    FreePool (Original);
    return;
  }
  Count = 0;
  Mask  = 0;
  Bsp   = ArmReadMpidr () & 0xFFFFFF;
  if (Bsp >= WINRT_SMP_MAX_CPUS) {
    goto Invalid;
  }
  DEBUG ((DEBUG_INFO, "WinRT SMP: MADT length=%u header=%u GICC=%u BSP=0x%x\n",
          Copy->Length, (UINT32)sizeof (EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER),
          (UINT32)sizeof (*Gic), (UINT32)Bsp));
  for (Offset = sizeof (EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);
       Offset + 2 <= Copy->Length; Offset += ((UINT8 *)Copy)[Offset + 1]) {
    UINT8 Length = ((UINT8 *)Copy)[Offset + 1];
    if ((Length < 2) || (Length > Copy->Length - Offset)) {
      goto Invalid;
    }
    if (((UINT8 *)Copy)[Offset] != EFI_ACPI_6_5_GIC) {
      continue;
    }
    // QEMU emits an 80-byte GICC, predating the optional ACPI 6.5 TRBE field.
    // Require all fields through MPIDR, rather than sizeof the newest structure.
    if (Length < OFFSET_OF (EFI_ACPI_6_5_GIC_STRUCTURE, ProcessorPowerEfficiencyClass)) {
      goto Invalid;
    }
    Gic = (VOID *)((UINT8 *)Copy + Offset);
    if ((Gic->Flags & 1) == 0) {
      continue;
    }
    Cpu = (UINTN)(Gic->MPIDR & 0xFFFFFF);
    if ((Cpu >= WINRT_SMP_MAX_CPUS) || (Cpu != Gic->CPUInterfaceNumber) ||
        ((Mask & (1U << Cpu)) != 0)) { goto Invalid; }
    Mask |= 1U << Cpu;
    Count++;
  }
  if ((Offset != Copy->Length) || ((Mask & (1U << Bsp)) == 0)) {
    goto Invalid;
  }
  if (Count == 1) {
    DEBUG ((DEBUG_INFO, "WinRT SMP: one CPU, parking not required\n"));
    goto Done;
  }
  CodeSize = (UINTN)WinRtParkingEnd - (UINTN)WinRtParkingStart;
  if (CodeSize > WINRT_SMP_READY_OFFSET - WINRT_SMP_CODE_OFFSET) {
    goto Invalid;
  }
  Pages = MAX_UINT32;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiACPIMemoryNVS,
                              WINRT_SMP_MAX_CPUS, &Pages);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }
  ZeroMem ((VOID *)(UINTN)Pages, EFI_PAGES_TO_SIZE (WINRT_SMP_MAX_CPUS));
  for (Cpu = 0; Cpu < WINRT_SMP_MAX_CPUS; Cpu++) {
    Page = (UINT8 *)(UINTN)Pages + EFI_PAGES_TO_SIZE (Cpu);
    *(UINT32 *)Page = MAX_UINT32;
    CopyMem (Page + WINRT_SMP_CODE_OFFSET, WinRtParkingStart, CodeSize);
  }
  WriteBackDataCacheRange ((VOID *)(UINTN)Pages, EFI_PAGES_TO_SIZE (WINRT_SMP_MAX_CPUS));
  InvalidateInstructionCacheRange ((VOID *)(UINTN)Pages, EFI_PAGES_TO_SIZE (WINRT_SMP_MAX_CPUS));
  for (Offset = sizeof (EFI_ACPI_6_5_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);
       Offset < Copy->Length; Offset += ((UINT8 *)Copy)[Offset + 1]) {
    if (((UINT8 *)Copy)[Offset] != EFI_ACPI_6_5_GIC) {
      continue;
    }
    Gic = (VOID *)((UINT8 *)Copy + Offset);
    if ((Gic->Flags & 1) == 0) {
      continue;
    }
    Cpu = (UINTN)(Gic->MPIDR & 0xFFFFFF);
    Gic->ParkingProtocolVersion = 1;
    Gic->ParkedAddress          = Pages + EFI_PAGES_TO_SIZE (Cpu);
  }
  Status = Acpi->UninstallAcpiTable (Acpi, OldKey);
  if (!EFI_ERROR (Status)) {
    Status = Acpi->InstallAcpiTable (Acpi, Copy, Copy->Length, &NewKey);
    if (EFI_ERROR (Status)) {
      Acpi->InstallAcpiTable (Acpi, Original, Original->Length, &NewKey);
    }
  }
  if (EFI_ERROR (Status)) {
    gBS->FreePages (Pages, WINRT_SMP_MAX_CPUS);
    goto Failed;
  }
  Release = (VOID *)(UINTN)FixedPcdGet32 (PcdWinRtSecondaryReleaseBase);
  for (Cpu = 0; Cpu < WINRT_SMP_MAX_CPUS; Cpu++) {
    if ((Cpu != Bsp) && ((Mask & (1U << Cpu)) != 0)) {
      Release[Cpu] = (UINT32)(Pages + EFI_PAGES_TO_SIZE (Cpu) + WINRT_SMP_CODE_OFFSET);
    }
  }
  WriteBackDataCacheRange ((VOID *)Release, EFI_PAGE_SIZE);
  ArmDataSynchronizationBarrier ();
  ArmGicSendSgiTo (WINRT_GIC_DIST_BASE, ARM_GIC_ICDSGIR_FILTER_TARGETLIST,
                   (UINT8)(Mask & ~(1U << Bsp)), 0);
  for (Cpu = 0; Cpu < WINRT_SMP_MAX_CPUS; Cpu++) {
    if ((Cpu == Bsp) || ((Mask & (1U << Cpu)) == 0)) {
      continue;
    }
    Ready = (VOID *)(UINTN)(Pages + EFI_PAGES_TO_SIZE (Cpu) + WINRT_SMP_READY_OFFSET);
    for (Retry = 0; Retry < 20000; Retry++) {
      InvalidateDataCacheRange ((VOID *)Ready, sizeof (*Ready));
      if (*Ready == 1) {
        break;
      }
      MicroSecondDelay (100);
    }
    DEBUG ((DEBUG_INFO, "WinRT SMP: CPU%u mailbox=0x%Lx ready=%u\n",
            (UINT32)Cpu, Pages + EFI_PAGES_TO_SIZE (Cpu), *Ready));
  }
  DEBUG ((DEBUG_INFO, "WinRT SMP: parking v1 CPUs=%u mask=0x%x base=0x%Lx\n",
          (UINT32)Count, (UINT32)Mask, Pages));
  goto Done;
Invalid:
  Status = EFI_UNSUPPORTED;
Failed:
  DEBUG ((DEBUG_ERROR, "WinRT SMP: parking setup failed: %r\n", Status));
Done:
  FreePool (Copy);
  FreePool (Original);
}

EFI_STATUS
EFIAPI
WinRtSmpEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT  Event;

  return EfiCreateEventReadyToBootEx (TPL_CALLBACK, PrepareWinRtParking, NULL, &Event);
}
