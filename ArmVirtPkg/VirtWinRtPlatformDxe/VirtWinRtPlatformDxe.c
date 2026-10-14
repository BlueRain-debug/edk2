/** @file
*  Enables Platform EHCI and SDHCI devices in QEMU virt-rt machine
*  For Windows RT
*
*  Copyright (c) 2021, GH Cao. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <Guid/PlatformHasAcpi.h>
#include <Guid/Acpi.h>
#include <IndustryStandard/Acpi65.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/BlockIo.h>
#include <Protocol/BlockIo2.h>
#include <Protocol/DiskIo.h>
#include <Protocol/FirmwareVolumeBlock.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

// Apply the alignment when Block I/O is installed, before DiskIo allocates
// its shared buffers using the advertised alignment.
// This is a diagnostic option for the observed 8288 loader buffer alias.
// It is disabled by default; changing alignment has not established reliable
// cold boots. No disk content or image verification is modified.
STATIC
VOID
EFIAPI
AlignWinRtSdBlockIo (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     Count;
  UINTN                     Index;
  UINT32                    Alignment;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_BLOCK_IO_PROTOCOL     *BlockIo;
  EFI_BLOCK_IO2_PROTOCOL    *BlockIo2;
  BOOLEAN                   IsSd;

  Alignment = PcdGet32 (PcdWinRtSdIoAlign);
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_FOUND) {
      DEBUG ((DEBUG_ERROR, "WinRT SD alignment: %r\n", Status));
    }
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiDevicePathProtocolGuid,
                    (VOID **) &Node
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    // The SD node remains in partition paths, so include those handles too.
    IsSd = FALSE;
    while (!IsDevicePathEnd (Node)) {
      if ((DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
          (DevicePathSubType (Node) == MSG_SD_DP)) {
        IsSd = TRUE;
        break;
      }
      Node = NextDevicePathNode (Node);
    }
    if (!IsSd) {
      continue;
    }

    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **) &BlockIo
                    );
    if (!EFI_ERROR (Status) && (BlockIo->Media != NULL) &&
        (BlockIo->Media->IoAlign < Alignment)) {
      DEBUG ((DEBUG_INFO, "WinRT SD alignment: %p %u -> %u\n",
              Handles[Index], BlockIo->Media->IoAlign, Alignment));
      BlockIo->Media->IoAlign = Alignment;
    }

    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiBlockIo2ProtocolGuid,
                    (VOID **) &BlockIo2
                    );
    if (!EFI_ERROR (Status) && (BlockIo2->Media != NULL) &&
        (BlockIo2->Media->IoAlign < Alignment)) {
      BlockIo2->Media->IoAlign = Alignment;
    }
  }

  FreePool (Handles);
}

// Modern VirtNorFlashDxe already publishes FVB without disk protocols.
// Verify the WinRT boot path retains that contract for both NOR banks.
STATIC CONST EFI_GUID mVirtNorFlashGuid = {
  0xe452cabd, 0x5fe1, 0x4d97, { 0x81, 0x61, 0xe8, 0x0e, 0xd6, 0xa4, 0x09, 0xa8 }
};

STATIC
VOID
EFIAPI
VerifyWinRtNorProtocols (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  EFI_HANDLE                *Handles;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  VOID                      *Interface;
  EFI_STATUS                Status;
  UINTN                     Count;
  UINTN                     Index;
  UINTN                     Banks;
  UINTN                     Fvb;
  UINTN                     Disks;

  Banks = 0;
  Fvb = 0;
  Disks = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiDevicePathProtocolGuid,
                                   NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WinRT NOR protocol audit: %r\n", Status));
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid,
                                 (VOID **)&Node);
    if (EFI_ERROR (Status) ||
        (DevicePathType (Node) != HARDWARE_DEVICE_PATH) ||
        (DevicePathSubType (Node) != HW_VENDOR_DP) ||
        (DevicePathNodeLength (Node) < sizeof (VENDOR_DEVICE_PATH)) ||
        !CompareGuid (&((VENDOR_DEVICE_PATH *)Node)->Guid, &mVirtNorFlashGuid)) {
      continue;
    }
    Banks++;
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
          &gEfiFirmwareVolumeBlockProtocolGuid, &Interface))) {
      Fvb++;
    }
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
          &gEfiBlockIoProtocolGuid, &Interface))) {
      Disks++;
    }
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
          &gEfiDiskIoProtocolGuid, &Interface))) {
      Disks++;
    }
  }
  FreePool (Handles);
  DEBUG ((DEBUG_INFO, "WinRT NOR: banks=%u FVB=%u disk-protocols=%u\n",
          (UINT32)Banks, (UINT32)Fvb, (UINT32)Disks));
  if ((Banks == 0) || (Fvb == 0) || (Disks != 0)) {
    DEBUG ((DEBUG_ERROR, "WinRT NOR protocol contract failed\n"));
  }
}

// Early Windows RT reads the virtual timer interrupt at offset 52 and its
// one-byte flags at offset 62 of EGIT. The later GTDT uses different offsets
// and reverses the interrupt-polarity bit. Publish both descriptions of the
// same hardware, so each guest can use the format it understands.
#pragma pack (1)
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER    Header;
  UINT64                         CounterControlBase;
  UINT32                         SecureTimerGsiv;
  UINT32                         NonSecureTimerGsiv;
  UINT32                         VirtualTimerGsiv;
  UINT32                         HypervisorTimerGsiv;
  UINT8                          SecureTimerFlags;
  UINT8                          NonSecureTimerFlags;
  UINT8                          VirtualTimerFlags;
  UINT8                          HypervisorTimerFlags;
  UINT32                         GlobalFlags;
} WINRT_EGIT_TABLE;
#pragma pack ()

STATIC_ASSERT (sizeof (WINRT_EGIT_TABLE) == 68, "EGIT table size");
STATIC_ASSERT (OFFSET_OF (WINRT_EGIT_TABLE, VirtualTimerGsiv) == 52, "EGIT IRQ offset");
STATIC_ASSERT (OFFSET_OF (WINRT_EGIT_TABLE, VirtualTimerFlags) == 62, "EGIT flags offset");

STATIC
UINT8
WinRtLegacyTimerFlags (
  IN UINT32  Flags
  )
{
  // Both layouts use bit 0 for edge triggering, but EGIT bit 1 means high.
  return (UINT8)((Flags ^ BIT1) & (BIT0 | BIT1));
}

STATIC
VOID
EFIAPI
InstallWinRtLegacyTimerTable (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER    *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  EFI_ACPI_DESCRIPTION_HEADER                   *Header;
  EFI_ACPI_6_5_GENERIC_TIMER_DESCRIPTION_TABLE   *Gtdt;
  EFI_ACPI_TABLE_PROTOCOL                       *Acpi;
  WINRT_EGIT_TABLE                              Egit;
  EFI_STATUS                                    Status;
  UINT64                                        Address;
  UINTN                                         Index;
  UINTN                                         Count;
  UINTN                                         Key;

  gBS->CloseEvent (Event);
  Status = EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp);
  if (EFI_ERROR (Status) || (Rsdp->Revision < 2) || (Rsdp->XsdtAddress == 0)) {
    DEBUG ((DEBUG_ERROR, "WinRT EGIT: ACPI 2.0 root unavailable\n"));
    return;
  }

  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  if ((Xsdt->Signature != SIGNATURE_32 ('X', 'S', 'D', 'T')) ||
      (Xsdt->Length < sizeof (*Xsdt)) ||
      (((Xsdt->Length - sizeof (*Xsdt)) % sizeof (Address)) != 0)) {
    DEBUG ((DEBUG_ERROR, "WinRT EGIT: invalid XSDT\n"));
    return;
  }

  Gtdt = NULL;
  Count = (Xsdt->Length - sizeof (*Xsdt)) / sizeof (Address);
  for (Index = 0; Index < Count; Index++) {
    CopyMem (&Address, (UINT8 *)Xsdt + sizeof (*Xsdt) + Index * sizeof (Address),
             sizeof (Address));
    Header = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Address;
    if (Header->Signature == SIGNATURE_32 ('E', 'G', 'I', 'T')) {
      DEBUG ((DEBUG_INFO, "WinRT EGIT: existing table retained\n"));
      return;
    }
    if ((Header->Signature == EFI_ACPI_6_5_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE) &&
        (Header->Length >= OFFSET_OF (EFI_ACPI_6_5_GENERIC_TIMER_DESCRIPTION_TABLE,
                                      CntReadBasePhysicalAddress))) {
      Gtdt = (EFI_ACPI_6_5_GENERIC_TIMER_DESCRIPTION_TABLE *)Header;
    }
  }

  if (Gtdt == NULL) {
    DEBUG ((DEBUG_ERROR, "WinRT EGIT: GTDT unavailable\n"));
    return;
  }

  ZeroMem (&Egit, sizeof (Egit));
  CopyMem (&Egit.Header, &Gtdt->Header, sizeof (Egit.Header));
  Egit.Header.Signature      = SIGNATURE_32 ('E', 'G', 'I', 'T');
  Egit.Header.Length         = sizeof (Egit);
  Egit.Header.Revision       = 1;
  Egit.Header.Checksum       = 0;
  Egit.CounterControlBase    = Gtdt->CntControlBasePhysicalAddress;
  Egit.SecureTimerGsiv       = Gtdt->SecurePL1TimerGSIV;
  Egit.NonSecureTimerGsiv    = Gtdt->NonSecurePL1TimerGSIV;
  Egit.VirtualTimerGsiv      = Gtdt->VirtualTimerGSIV;
  Egit.HypervisorTimerGsiv   = Gtdt->NonSecurePL2TimerGSIV;
  Egit.SecureTimerFlags      = WinRtLegacyTimerFlags (Gtdt->SecurePL1TimerFlags);
  Egit.NonSecureTimerFlags   = WinRtLegacyTimerFlags (Gtdt->NonSecurePL1TimerFlags);
  Egit.VirtualTimerFlags     = WinRtLegacyTimerFlags (Gtdt->VirtualTimerFlags);
  Egit.HypervisorTimerFlags  = WinRtLegacyTimerFlags (Gtdt->NonSecurePL2TimerFlags);

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&Acpi);
  if (!EFI_ERROR (Status)) {
    Status = Acpi->InstallAcpiTable (Acpi, &Egit, sizeof (Egit), &Key);
  }
  DEBUG ((DEBUG_INFO, "WinRT EGIT: virtual IRQ=%u flags=0x%x install=%r\n",
          Egit.VirtualTimerGsiv, Egit.VirtualTimerFlags, Status));
}

EFI_STATUS
EFIAPI
InitializeVirtWinRtPlatformDxe (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_STATUS                     Status;
  UINTN                          EhciSize;
  VOID*                          EhciBase;
  UINTN                          SdHciSize;
  VOID*                          SdHciBase;
  UINT32                         Alignment;
  EFI_EVENT                      BlockIoEvent;
  VOID                           *Registration;
  EFI_EVENT                      ReadyEvent;

  DEBUG ((DEBUG_INFO, "%a: InitializeVirtWinRtPlatformDxe called\n", __FUNCTION__));

  EhciBase = (VOID*)(UINTN)PcdGet32 (PcdPlatformEhciBase);
  ASSERT (EhciBase != NULL);
  EhciSize = (UINTN)PcdGet32 (PcdPlatformEhciSize);
  ASSERT (EhciSize != 0);

  DEBUG ((DEBUG_INFO, "%a: Got platform EHCI %p %u\n",
          __FUNCTION__, EhciBase, EhciSize));

  Status = RegisterNonDiscoverableMmioDevice (
                   NonDiscoverableDeviceTypeEhci,
                   NonDiscoverableDeviceDmaTypeCoherent,
                   NULL,
                   NULL,
                   1,
                   EhciBase, EhciSize);

  if (EFI_ERROR(Status)) {
    DEBUG ((DEBUG_ERROR, "%a: NonDiscoverable: Cannot install EHCI device @%p (Staus == %r)\n",
            __FUNCTION__, EhciBase, Status));
    return Status;
  }

  SdHciBase = (VOID*)(UINTN)PcdGet32 (PcdPlatformSdHciBase);
  ASSERT (SdHciBase != NULL);
  SdHciSize = (UINTN)PcdGet32 (PcdPlatformSdHciSize);
  ASSERT (SdHciSize != 0);

  DEBUG ((DEBUG_INFO, "%a: Got platform SDHCI %p %u\n",
          __FUNCTION__, SdHciBase, SdHciSize));

  Status = RegisterNonDiscoverableMmioDevice (
                   NonDiscoverableDeviceTypeSdhci,
                   NonDiscoverableDeviceDmaTypeCoherent,
                   NULL,
                   NULL,
                   1,
                   SdHciBase, SdHciSize);

  if (EFI_ERROR(Status)) {
    DEBUG ((DEBUG_ERROR, "%a: NonDiscoverable: Cannot install SDHCI device @%p (Staus == %r)\n",
            __FUNCTION__, SdHciBase, Status));
    return Status;
  }

  Alignment = PcdGet32 (PcdWinRtSdIoAlign);
  if (Alignment > 1) {
    if ((Alignment & (Alignment - 1)) != 0) {
      return EFI_INVALID_PARAMETER;
    }
    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    AlignWinRtSdBlockIo,
                    NULL,
                    &BlockIoEvent
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Status = gBS->RegisterProtocolNotify (
                    &gEfiBlockIoProtocolGuid,
                    BlockIoEvent,
                    &Registration
                    );
    if (EFI_ERROR (Status)) {
      gBS->CloseEvent (BlockIoEvent);
      return Status;
    }
    gBS->SignalEvent (BlockIoEvent);
  }

  Status = EfiCreateEventReadyToBootEx (TPL_CALLBACK, VerifyWinRtNorProtocols,
                                      NULL, &ReadyEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (FixedPcdGetBool (PcdWinRtLegacyEgit)) {
    Status = EfiCreateEventReadyToBootEx (TPL_CALLBACK, InstallWinRtLegacyTimerTable,
                                        NULL, &ReadyEvent);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  // Windows RT consumes ACPI even though this is an AArch32 platform.
  return gBS->InstallProtocolInterface (
                &ImageHandle,
                &gEdkiiPlatformHasAcpiGuid,
                EFI_NATIVE_INTERFACE,
                NULL
                );
}
