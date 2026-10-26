/** @file
  Microsoft boot-environment battery and display power interfaces.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef WINRT_BOOT_POWER_H_
#define WINRT_BOOT_POWER_H_

#include <Uefi.h>

// Layouts follow Microsoft's EFI_BATTERY_CHARGING_PROTOCOL and
// EFI_DISPLAY_POWER_PROTOCOL bring-up specifications. These are boot services
// interfaces; they do not describe an ACPI battery to the running OS.
typedef struct _EFI_BATTERY_CHARGING_PROTOCOL EFI_BATTERY_CHARGING_PROTOCOL;
typedef struct _EFI_DISPLAY_POWER_PROTOCOL    EFI_DISPLAY_POWER_PROTOCOL;

typedef enum {
  EfiBatteryChargingStatusNone = 0,
  EfiBatteryChargingStatusSuccess,
  EfiBatteryChargingStatusOverheat,
  EfiBatteryChargingStatusVoltageOutOfRange,
  EfiBatteryChargingStatusCurrentOutOfRange,
  EfiBatteryChargingStatusTimeout,
  EfiBatteryChargingStatusAborted,
  EfiBatteryChargingStatusDeviceError,
  EfiBatteryChargingStatusExtremeCold,
  EfiBatteryChargingStatusBatteryChargingNotSupported,
  EfiBatteryChargingStatusBatteryNotDetected,
  EfiBatteryChargingSourceNotDetected,
  EfiBatteryChargingSourceVoltageInvalid,
  EfiBatteryChargingSourceCurrentInvalid,
  EfiBatteryChargingErrorRequestShutdown,
  EfiBatteryChargingErrorRequestReboot
} EFI_BATTERY_CHARGING_STATUS;

typedef struct {
  EFI_EVENT                      Event;
  EFI_BATTERY_CHARGING_STATUS     Status;
} EFI_BATTERY_CHARGING_COMPLETION_TOKEN;

typedef
EFI_STATUS
(EFIAPI *EFI_BATTERY_CHARGING_GET_BATTERY_STATUS)(
  IN EFI_BATTERY_CHARGING_PROTOCOL *This,
  OUT UINT32                      *StateOfCharge,
  OUT UINT32                      *RatedCapacity,
  OUT INT32                       *ChargeCurrent
  );

typedef
EFI_STATUS
(EFIAPI *EFI_BATTERY_CHARGING_CHARGE_BATTERY)(
  IN EFI_BATTERY_CHARGING_PROTOCOL          *This,
  IN UINT32                               MaximumCurrent,
  IN UINT32                               TargetStateOfCharge,
  IN EFI_BATTERY_CHARGING_COMPLETION_TOKEN  *CompletionToken OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_BATTERY_CHARGING_GET_BATTERY_INFORMATION)(
  IN EFI_BATTERY_CHARGING_PROTOCOL *This,
  OUT UINT32                      *StateOfCharge,
  OUT INT32                       *CurrentIntoBattery,
  OUT UINT32                      *BatteryTerminalVoltage,
  OUT INT32                       *BatteryTemperature,
  OUT UINT32                      *UsbCableVoltage,
  OUT UINT32                      *UsbCableCurrent
  );

struct _EFI_BATTERY_CHARGING_PROTOCOL {
  EFI_BATTERY_CHARGING_GET_BATTERY_STATUS       GetBatteryStatus;
  EFI_BATTERY_CHARGING_CHARGE_BATTERY           ChargeBattery;
  UINT32                                      Revision;
  EFI_BATTERY_CHARGING_GET_BATTERY_INFORMATION  GetBatteryInformation;
};

typedef enum {
  EfiDisplayPowerStateUnknown = 0,
  EfiDisplayPowerStateOff,
  EfiDisplayPowerStateMaximum
} EFI_DISPLAY_POWER_STATE;

typedef
EFI_STATUS
(EFIAPI *EFI_DISPLAY_POWER_SETDISPLAYPOWERSTATE)(
  IN EFI_DISPLAY_POWER_PROTOCOL *This,
  IN EFI_DISPLAY_POWER_STATE    PowerState
  );

typedef
EFI_STATUS
(EFIAPI *EFI_DISPLAY_POWER_GETDISPLAYPOWERSTATE)(
  IN EFI_DISPLAY_POWER_PROTOCOL *This,
  OUT EFI_DISPLAY_POWER_STATE   *PowerState
  );

struct _EFI_DISPLAY_POWER_PROTOCOL {
  UINT32                                   Revision;
  EFI_DISPLAY_POWER_SETDISPLAYPOWERSTATE    SetDisplayPowerState;
  EFI_DISPLAY_POWER_GETDISPLAYPOWERSTATE    GetDisplayPowerState;
};

extern EFI_GUID  gWinRtBatteryChargingProtocolGuid;
extern EFI_GUID  gWinRtDisplayPowerProtocolGuid;

#endif
