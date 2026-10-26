/** @file
  Optional, always-full virtual battery for the QEMU Windows ARM platform.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/PciIo.h>
#include <Protocol/WinRtBootPower.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC EFI_DISPLAY_POWER_STATE  mDisplayState = EfiDisplayPowerStateMaximum;
STATIC UINT32                   mStatusReads;
STATIC UINT32                   mInformationReads;

STATIC
EFI_STATUS
EFIAPI
GetBatteryStatus (
  IN EFI_BATTERY_CHARGING_PROTOCOL  *This,
  OUT UINT32                       *StateOfCharge,
  OUT UINT32                       *RatedCapacity,
  OUT INT32                        *ChargeCurrent
  )
{
  if ((This == NULL) || (StateOfCharge == NULL) ||
      (RatedCapacity == NULL) || (ChargeCurrent == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *StateOfCharge = 100;
  *RatedCapacity = 3000;
  *ChargeCurrent = 0;
  if (mStatusReads++ < 3) {
    DEBUG ((DEBUG_INFO, "WinRT boot battery: GetBatteryStatus SOC=100 capacity=3000mAh current=0mA\n"));
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GetBatteryInformation (
  IN EFI_BATTERY_CHARGING_PROTOCOL  *This,
  OUT UINT32                       *StateOfCharge,
  OUT INT32                        *CurrentIntoBattery,
  OUT UINT32                       *BatteryTerminalVoltage,
  OUT INT32                        *BatteryTemperature,
  OUT UINT32                       *UsbCableVoltage,
  OUT UINT32                       *UsbCableCurrent
  )
{
  if ((This == NULL) || (StateOfCharge == NULL) ||
      (CurrentIntoBattery == NULL) || (BatteryTerminalVoltage == NULL) ||
      (BatteryTemperature == NULL) || (UsbCableVoltage == NULL) ||
      (UsbCableCurrent == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *StateOfCharge          = 100;
  *CurrentIntoBattery    = 0;
  *BatteryTerminalVoltage = 4200;
  *BatteryTemperature    = 2981; // Tenths of kelvin, approximately 25 C.
  *UsbCableVoltage       = 5000;
  *UsbCableCurrent       = 0;
  if (mInformationReads++ < 3) {
    DEBUG ((DEBUG_INFO, "WinRT boot battery: GetBatteryInformation SOC=100 voltage=4200mV temperature=2981dK\n"));
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ChargeBattery (
  IN EFI_BATTERY_CHARGING_PROTOCOL          *This,
  IN UINT32                               MaximumCurrent,
  IN UINT32                               TargetStateOfCharge,
  IN EFI_BATTERY_CHARGING_COMPLETION_TOKEN  *CompletionToken OPTIONAL
  )
{
  if ((This == NULL) || (TargetStateOfCharge > 100) ||
      ((CompletionToken != NULL) && (CompletionToken->Event == NULL))) {
    return EFI_INVALID_PARAMETER;
  }

  // Every valid target is already satisfied. Complete the optional event
  // request without queuing a timer or retaining a caller-owned token.
  DEBUG ((DEBUG_INFO, "WinRT boot battery: ChargeBattery target=%u maximum=%umA already full\n",
          TargetStateOfCharge, MaximumCurrent));
  if (CompletionToken != NULL) {
    CompletionToken->Status = EfiBatteryChargingStatusSuccess;
    return gBS->SignalEvent (CompletionToken->Event);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SetVgaBlanking (
  IN BOOLEAN  Blank
  )
{
  EFI_STATUS           Status;
  EFI_STATUS           RestoreStatus;
  EFI_HANDLE           *Handles;
  UINTN                Count;
  UINTN                Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT32               Id;
  UINT8                Misc;
  UINT8                AttributeIndex;
  UINT8                Data;
  UINT16               StatusPort;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_UNSUPPORTED;
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid,
                                        (VOID **)&PciIo))) {
      continue;
    }

    if (EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, 1, &Id)) ||
        (Id != 0x11111234)) {
      continue;
    }

    // QEMU standard VGA blanks its output when attribute-controller index
    // bit 5 is clear. Preserve the palette index and reset the index/data
    // flip-flop after the operation; framebuffer contents are untouched.
    Status = PciIo->Io.Read (PciIo, EfiPciIoWidthUint8,
                             EFI_PCI_IO_PASS_THROUGH_BAR, 0x3cc, 1, &Misc);
    if (EFI_ERROR (Status)) {
      break;
    }

    StatusPort = (Misc & BIT0) ? 0x3da : 0x3ba;
    Status = PciIo->Io.Read (PciIo, EfiPciIoWidthUint8,
                             EFI_PCI_IO_PASS_THROUGH_BAR, StatusPort, 1, &Data);
    if (EFI_ERROR (Status)) {
      break;
    }

    Status = PciIo->Io.Read (PciIo, EfiPciIoWidthUint8,
                             EFI_PCI_IO_PASS_THROUGH_BAR, 0x3c0, 1, &AttributeIndex);
    if (EFI_ERROR (Status)) {
      break;
    }

    AttributeIndex = Blank ? (AttributeIndex & ~BIT5) : (AttributeIndex | BIT5);
    Status = PciIo->Io.Write (PciIo, EfiPciIoWidthUint8,
                              EFI_PCI_IO_PASS_THROUGH_BAR, 0x3c0, 1, &AttributeIndex);
    RestoreStatus = PciIo->Io.Read (PciIo, EfiPciIoWidthUint8,
                                    EFI_PCI_IO_PASS_THROUGH_BAR, StatusPort, 1, &Data);
    if (!EFI_ERROR (Status)) {
      Status = RestoreStatus;
    }

    break;
  }

  FreePool (Handles);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
SetDisplayPowerState (
  IN EFI_DISPLAY_POWER_PROTOCOL  *This,
  IN EFI_DISPLAY_POWER_STATE     PowerState
  )
{
  EFI_STATUS  Status;

  if ((This == NULL) || ((PowerState != EfiDisplayPowerStateOff) &&
                         (PowerState != EfiDisplayPowerStateMaximum))) {
    return EFI_INVALID_PARAMETER;
  }

  if (PowerState == mDisplayState) {
    return EFI_SUCCESS;
  }

  Status = SetVgaBlanking (PowerState == EfiDisplayPowerStateOff);
  DEBUG ((DEBUG_INFO, "WinRT display power: state %u -> %u: %r\n",
          mDisplayState, PowerState, Status));
  if (!EFI_ERROR (Status)) {
    mDisplayState = PowerState;
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
GetDisplayPowerState (
  IN EFI_DISPLAY_POWER_PROTOCOL  *This,
  OUT EFI_DISPLAY_POWER_STATE    *PowerState
  )
{
  if ((This == NULL) || (PowerState == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *PowerState = mDisplayState;
  return EFI_SUCCESS;
}

STATIC EFI_BATTERY_CHARGING_PROTOCOL  mBattery = {
  GetBatteryStatus,
  ChargeBattery,
  0x00010002,
  GetBatteryInformation
};

STATIC EFI_DISPLAY_POWER_PROTOCOL  mDisplayPower = {
  0x00010000,
  SetDisplayPowerState,
  GetDisplayPowerState
};

STATIC_ASSERT (OFFSET_OF (EFI_BATTERY_CHARGING_PROTOCOL, Revision) ==
               2 * sizeof (VOID *), "Battery ABI places revision after callbacks");

EFI_STATUS
EFIAPI
InitializeWinRtBootPower (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gWinRtBatteryChargingProtocolGuid, &mBattery,
                  &gWinRtDisplayPowerProtocolGuid, &mDisplayPower,
                  NULL
                  );
  DEBUG ((DEBUG_INFO, "WinRT boot power: install virtual full battery and display power: %r\n", Status));
  return Status;
}
