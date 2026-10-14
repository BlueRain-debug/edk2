/** @file
  Private layout shared by the WinRT SEC and ACPI parking driver.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef WINRT_SMP_H_
#define WINRT_SMP_H_
#define WINRT_SMP_MAX_CPUS       4
#define WINRT_SMP_CODE_OFFSET    0x800
#define WINRT_SMP_READY_OFFSET   0xF00
#define WINRT_GIC_CPU_BASE       0x08010000
#define WINRT_GIC_DIST_BASE      0x08000000
#endif
