/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0xffffffc050500000
#define KERNEL_STACK_2  0xffffffc050501000
.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw  CSR_SIE, zero
  csrw  CSR_SIP, zero

  /* Check ID */
  csrr  a0, CSR_MHARTID
  bnez  a0, secondary_start

  /* TODO: [p1-task2] clear BSS for flat non-ELF images */
  la    t0, __bss_start
  la    t1, __BSS_END__

clear_bss:
  sw    zero, 0(t0)
  addi  t0, t0, 4
  blt   t0, t1, clear_bss

  /* TODO: [p1-task2] setup C environment */
end_clear_bss:
  la    tp, pid0_pcb
  li    sp, KERNEL_STACK
  call  main

secondary_start:
  /* Slave Core Stack Setup */
  /* Use a different stack area to avoid conflict */
  li    sp, KERNEL_STACK_2
  call  main

loop:
  wfi
  j loop

END(_start)
