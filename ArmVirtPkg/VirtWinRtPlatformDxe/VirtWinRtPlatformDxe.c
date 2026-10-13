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

  // Windows RT consumes ACPI even though this is an AArch32 platform.
  return gBS->InstallProtocolInterface (
                &ImageHandle,
                &gEdkiiPlatformHasAcpiGuid,
                EFI_NATIVE_INTERFACE,
                NULL
                );
}
