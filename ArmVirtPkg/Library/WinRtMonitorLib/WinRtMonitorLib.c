/** @file
  No SMCCC provider is present in the single-CPU Windows RT experiment.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Base.h>
#include <Library/ArmMonitorLib.h>

VOID
EFIAPI
ArmMonitorCall (
  IN OUT ARM_MONITOR_ARGS  *Args
  )
{
  // Report SMCCC NOT_SUPPORTED without issuing an unavailable HVC/SMC.
  Args->Arg0 = (UINTN)-1;
  Args->Arg1 = 0;
  Args->Arg2 = 0;
  Args->Arg3 = 0;
  Args->Arg4 = 0;
  Args->Arg5 = 0;
  Args->Arg6 = 0;
  Args->Arg7 = 0;
}
