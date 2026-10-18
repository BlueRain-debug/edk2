/** @file
  Runtime reset and shutdown for QEMU's PSCI-free Windows RT platform.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Protocol/FdtClient.h>

#define PL061_DIRECTION  0x400
#define WINRT_POWER_OFF  BIT0
#define WINRT_RESET      BIT1

STATIC VOID  *mResetGpio;

STATIC
VOID
EFIAPI
WinRtResetVirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  Status = EfiConvertPointer (0, &mResetGpio);
  ASSERT_EFI_ERROR (Status);
}

EFI_STATUS
EFIAPI
WinRtResetSystemLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  FDT_CLIENT_PROTOCOL               *FdtClient;
  CONST UINT64                      *Reg;
  INT32                             Node;
  UINT32                            RegSize;
  UINT64                            Base;
  EFI_STATUS                        Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR   Descriptor;
  EFI_EVENT                         Event;

  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&FdtClient);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FdtClient->FindCompatibleNode (FdtClient, "qemu,winrt-reset-gpio", &Node);
  if (EFI_ERROR (Status)) {
    // Older QEMU builds do not expose this optional platform interface.
    DEBUG ((DEBUG_WARN, "WinRT reset: QEMU GPIO interface unavailable\n"));
    return EFI_SUCCESS;
  }

  Status = FdtClient->GetNodeProperty (FdtClient, Node, "reg", (CONST VOID **)&Reg, &RegSize);
  if (EFI_ERROR (Status) || (RegSize != 2 * sizeof (UINT64))) {
    return EFI_UNSUPPORTED;
  }

  Base = SwapBytes64 (ReadUnaligned64 (&Reg[0]));
  if ((Base == 0) || (Base > MAX_UINTN - (SIZE_4KB - 1)) ||
      ((Base & (SIZE_4KB - 1)) != 0) ||
      (SwapBytes64 (ReadUnaligned64 (&Reg[1])) < SIZE_4KB)) {
    return EFI_UNSUPPORTED;
  }

  Status = gDS->GetMemorySpaceDescriptor (Base, &Descriptor);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Descriptor.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    Status = gDS->AddMemorySpace (
                    EfiGcdMemoryTypeMemoryMappedIo,
                    Base,
                    SIZE_4KB,
                    EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else if (Descriptor.GcdMemoryType != EfiGcdMemoryTypeMemoryMappedIo) {
    return EFI_UNSUPPORTED;
  }

  Status = gDS->SetMemorySpaceAttributes (
                  Base,
                  SIZE_4KB,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mResetGpio = (VOID *)(UINTN)Base;
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  WinRtResetVirtualAddressChange,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    mResetGpio = NULL;
    return Status;
  }

  // Firmware owns this controller; OS drivers must not reconfigure its pins.
  Status = FdtClient->SetNodeProperty (FdtClient, Node, "status", "disabled", sizeof ("disabled"));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "WinRT reset: failed to hide GPIO from OS: %r\n", Status));
  }

  DEBUG ((DEBUG_INFO, "WinRT reset: runtime GPIO at 0x%Lx\n", Base));
  return EFI_SUCCESS;
}

STATIC
VOID
WinRtRequestPowerAction (
  IN UINT32  Pin
  )
{
  UINTN  Base;

  if (mResetGpio == NULL) {
    return;
  }

  Base = (UINTN)mResetGpio;
  // PL061 masked DATA registers are addressed by the pin mask shifted by two.
  MmioOr32 (Base + PL061_DIRECTION, Pin);
  MmioWrite32 (Base + (Pin << 2), Pin);
  CpuDeadLoop ();
}

VOID
EFIAPI
ResetCold (
  VOID
  )
{
  WinRtRequestPowerAction (WINRT_RESET);
}

VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  ResetCold ();
}

VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  WinRtRequestPowerAction (WINRT_POWER_OFF);
}

VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData
  )
{
  ResetCold ();
}

VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetCold:
    case EfiResetWarm:
    case EfiResetPlatformSpecific:
      ResetCold ();
      break;
    case EfiResetShutdown:
      ResetShutdown ();
      break;
    default:
      break;
  }
}
