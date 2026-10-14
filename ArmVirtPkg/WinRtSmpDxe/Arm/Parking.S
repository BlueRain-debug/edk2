// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <AsmMacroLib.h>
#include <WinRtSmp.h>

ASM_FUNC(WinRtParkingStart)
  // This entire fragment is copied into the firmware half of a 4 KiB page.
  // It must remain stackless and contain no references outside the fragment.
  adr   r5, ASM_PFX(WinRtParkingStart)
  sub   r5, r5, #WINRT_SMP_CODE_OFFSET
  mrc   p15, 0, r6, c0, c0, 5
  and   r6, r6, #0xff
  mrc   p15, 0, r1, c1, c0, 1
  orr   r1, r1, #0x40
  mcr   p15, 0, r1, c1, c0, 1
  isb
  mov   r1, #1
  str   r1, [r5, #WINRT_SMP_READY_OFFSET]
  dsb   sy
.Lpark:
  // Leave the wake interrupt pending for the OS to acknowledge.
  dsb   sy
  ldr   r1, [r5]
  cmp   r1, r6
  bne   .Lwait
  ldr   r12, [r5, #8]
  ldr   r1, [r5, #12]
  cmp   r1, #0
  bne   .Lwait
  cmp   r12, #0
  beq   .Lwait
  mov   r1, #0
  str   r1, [r5, #8]
  str   r1, [r5, #12]
  dsb   sy
  isb
  mov   r0, r5
  mov   r2, #0
  mov   r3, #0
  bx    r12
.Lwait:
  wfi
  b     .Lpark
  .global ASM_PFX(WinRtParkingEnd)
ASM_PFX(WinRtParkingEnd):
