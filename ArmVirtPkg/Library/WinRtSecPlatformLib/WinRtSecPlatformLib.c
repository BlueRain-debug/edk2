/** @file
  Platform callbacks for the WinRT SEC instance.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/ArmPlatformLib.h>

RETURN_STATUS
ArmPlatformInitialize (
  IN UINTN  MpId
  )
{
  return RETURN_SUCCESS;
}

VOID
ArmPlatformGetPlatformPpiList (
  OUT UINTN                   *PpiListSize,
  OUT EFI_PEI_PPI_DESCRIPTOR   **PpiList
  )
{
  // CPU discovery for this platform comes from the QEMU ACPI MADT.
  *PpiListSize = 0;
  *PpiList     = NULL;
}
