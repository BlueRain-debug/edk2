// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <AsmMacroLib.h>
#include <WinRtSmp.h>

ASM_FUNC(ArmPlatformPeiBootAction)
  mrc   p15, 0, r0, c0, c0, 5
  and   r0, r0, #0xff
  MOV32 (r1, FixedPcdGet32 (PcdWinRtSecondaryReleaseBase))
  cmp   r0, #0
  bne   .Lsecondary

  // Only the BSP may enter SEC's shared stack and temporary heap.
  mov   r2, #0
  mov   r3, #WINRT_SMP_MAX_CPUS
.Lclear:
  str   r2, [r1], #4
  subs  r3, r3, #1
  bne   .Lclear
  dsb   sy
  bx    lr

.Lsecondary:
  cpsid aif
  cmp   r0, #WINRT_SMP_MAX_CPUS
  bhs   .Lunsupported
  add   r4, r1, r0, lsl #2
  // Mirror the BSP's UEFI setup for the banked SGI/PPI registers.
  MOV32 (r6, WINRT_GIC_DIST_BASE)
  MOV32 (r1, 0xffff0000)
  str   r1, [r6, #0x180]
  mov   r1, #0
  str   r1, [r6, #0xc04]
  add   r2, r6, #0x400
  mov   r3, #8
  MOV32 (r1, 0x80808080)
.Lpriority:
  str   r1, [r2], #4
  subs  r3, r3, #1
  bne   .Lpriority
  MOV32 (r5, WINRT_GIC_CPU_BASE)
  mov   r1, #7
  str   r1, [r5, #8]
  mov   r1, #0xff
  str   r1, [r5, #4]
  mov   r1, #1
  str   r1, [r5]
  dsb   sy
.Lwait:
  // Require a fresh interrupt before trusting RAM left over after reset.
  wfi
  ldr   r1, [r5, #12]
  ubfx  r2, r1, #0, #10
  cmp   r2, #1020
  bhs   .Lwait
  str   r1, [r5, #16]
  dsb   sy
  ldr   r2, [r4]
  cmp   r2, #0
  beq   .Lwait
  dsb   sy
  isb
  bx    r2
.Lunsupported:
  wfi
  b     .Lunsupported
