/** @file
*
*  Copyright (c) 2011-2014, ARM Limited. All rights reserved.
*  Copyright (c) 2014, Linaro Limited. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <PiPei.h>

#include <Library/ArmMmuLib.h>
#include <Library/BaseMemoryLib.h>
#include <libfdt.h>
#include <Library/ArmVirtMemInfoLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/CacheMaintenanceLib.h>

STATIC CONST EFI_PHYSICAL_ADDRESS mTegraPages[] = {
  0x60005000, 0x7000E000
};

STATIC CONST EFI_PHYSICAL_ADDRESS mOmapPages[] = {
  0x48032000, 0x48034000, 0x48036000,
  0x4A009000, 0x4A307000, 0x4A318000
};

STATIC
CONST EFI_PHYSICAL_ADDRESS *
WinRtGetTimerPages (
  OUT UINTN  *Count
  )
{
  CONST VOID  *Fdt;

  Fdt = (VOID *)(UINTN)PcdGet64 (PcdDeviceTreeInitialBaseAddress);
  *Count = 0;
  if (fdt_check_header (Fdt) != 0) {
    return NULL;
  }

  if (fdt_getprop (Fdt, 0, "qemu,winrt-omap-timer", NULL) != NULL) {
    *Count = ARRAY_SIZE (mOmapPages);
    return mOmapPages;
  }

  if (fdt_getprop (Fdt, 0, "qemu,winrt-tegra-timer", NULL) != NULL) {
    *Count = ARRAY_SIZE (mTegraPages);
    return mTegraPages;
  }

  return NULL;
}

STATIC
VOID
BuildWinRtRamHobs (
  IN UINT64                       Base,
  IN UINT64                       Size,
  IN EFI_RESOURCE_ATTRIBUTE_TYPE  Attributes
  )
{
  UINT64  LowSize;

  if (Size == 0) {
    return;
  }

  if ((Base <= MAX_ALLOC_ADDRESS) && (Base + Size - 1 > MAX_ALLOC_ADDRESS)) {
    LowSize = (UINT64)MAX_ALLOC_ADDRESS - Base + 1;
    BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, Attributes,
                                Base, LowSize);
    Base += LowSize;
    Size -= LowSize;
  }

  BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, Attributes,
                              Base, Size);
}

VOID
BuildMemoryTypeInformationHob (
  VOID
  );

VOID
InitMmu (
  VOID
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable;
  VOID                          *TranslationTableBase;
  UINTN                         TranslationTableSize;
  RETURN_STATUS                 Status;
  ARM_MEMORY_REGION_DESCRIPTOR  *TimerMemoryTable;
  CONST EFI_PHYSICAL_ADDRESS    *TimerPages;
  UINTN                         TimerPageCount;
  UINTN                         Count;
  UINTN                         Index;

  // Get Virtual Memory Map from the Platform Library
  ArmVirtGetMemoryMap (&MemoryTable);

  TimerPages = WinRtGetTimerPages (&TimerPageCount);
  if (TimerPageCount != 0) {
    for (Count = 0; MemoryTable[Count].Length != 0; Count++) {
    }

    TimerMemoryTable = AllocateZeroPool (
                         (Count + TimerPageCount + 1) *
                         sizeof (*MemoryTable)
                         );
    ASSERT (TimerMemoryTable != NULL);
    if (TimerMemoryTable == NULL) {
      return;
    }

    CopyMem (TimerMemoryTable, MemoryTable, Count * sizeof (*MemoryTable));
    for (Index = 0; Index < TimerPageCount; Index++) {
      TimerMemoryTable[Count + Index].PhysicalBase = TimerPages[Index];
      TimerMemoryTable[Count + Index].VirtualBase  = TimerPages[Index];
      TimerMemoryTable[Count + Index].Length       = EFI_PAGE_SIZE;
      TimerMemoryTable[Count + Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
    }

    MemoryTable = TimerMemoryTable;
  }

  // Note: Because we called PeiServicesInstallPeiMemory() before to call InitMmu() the MMU Page Table resides in
  //      DRAM (even at the top of DRAM as it is the first permanent memory allocation)
  Status = ArmConfigureMmu (MemoryTable, &TranslationTableBase, &TranslationTableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Error: Failed to enable MMU\n"));
  }
}

EFI_STATUS
EFIAPI
MemoryPeim (
  IN EFI_PHYSICAL_ADDRESS  UefiMemoryBase,
  IN UINT64                UefiMemorySize
  )
{
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttributes;
  UINT64                       SystemMemoryTop;
  UINT64                       SystemMemorySize;
  VOID                         *Hob;
  UINT64                       NextBase;
  UINTN                        Index;
  CONST EFI_PHYSICAL_ADDRESS   *TimerPages;
  UINTN                        TimerPageCount;

  // Ensure PcdSystemMemorySize has been set
  ASSERT (PcdGet64 (PcdSystemMemorySize) != 0);

  SystemMemorySize = PcdGet64 (PcdSystemMemorySize);

  Hob = GetFirstGuidHob (&gArmVirtSystemMemorySizeGuid);
  if (Hob != NULL) {
    SystemMemorySize = *(UINT64 *)GET_GUID_HOB_DATA (Hob);
  }

  //
  // Now, the permanent memory has been installed, we can call AllocatePages()
  //
  ResourceAttributes = (
                        EFI_RESOURCE_ATTRIBUTE_PRESENT |
                        EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_TESTED
                        );

  // Windows RT 8288 allocates uncached page-table pages when TTBR0 has
  // no cacheability bits. Advertise the RAM capability without changing
  // the firmware's WB mappings. This platform is intended for QEMU TCG;
  // KVM guests must retain the upstream WB-only capability policy.
  if (FixedPcdGetBool (PcdWinRtUncachedRam)) {
    ResourceAttributes |= EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE;
  }

  SystemMemoryTop = PcdGet64 (PcdSystemMemoryBase) + SystemMemorySize;

  NextBase = PcdGet64 (PcdSystemMemoryBase);
  TimerPages = WinRtGetTimerPages (&TimerPageCount);
  if (TimerPageCount != 0) {
    for (Index = 0; Index < TimerPageCount; Index++) {
      // The fixed HAL addresses can overlap virt RAM. Exclude the pages
      // before PEI/DXE can allocate them.
      if ((TimerPages[Index] >= NextBase) &&
          (TimerPages[Index] < SystemMemoryTop)) {
        BuildWinRtRamHobs (NextBase, TimerPages[Index] - NextBase,
                           ResourceAttributes);
        NextBase = TimerPages[Index] + EFI_PAGE_SIZE;
      }

      BuildResourceDescriptorHob (
        EFI_RESOURCE_MEMORY_MAPPED_IO,
        EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
        EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE,
        TimerPages[Index],
        EFI_PAGE_SIZE
        );
      DEBUG ((DEBUG_INFO, "WinRT: reserved timer MMIO page at 0x%lx\n",
              TimerPages[Index]));
    }
  }

  BuildWinRtRamHobs (NextBase, SystemMemoryTop - NextBase, ResourceAttributes);

  // Keep the SEC secondary-release words out of the PEI/DXE page allocator.
  BuildMemoryAllocationHob (FixedPcdGet32 (PcdWinRtSecondaryReleaseBase),
                            EFI_PAGE_SIZE, EfiReservedMemoryType);

  // Build Memory Allocation Hob
  InitMmu ();

  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob ();
  }

  return EFI_SUCCESS;
}
