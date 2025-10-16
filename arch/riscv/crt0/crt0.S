#include <asm.h>

.section ".entry_function","ax"
ENTRY(_start)

    /* TODO: [p1-task3] setup C runtime environment for the user program */
    la      t0, __bss_start
    la      t1, __BSS_END__

clear_bss:
    sw      zero, 0(t0)
    addi    t0, t0, 4
    blt     t0, t1, clear_bss


    /* TODO: [p1-task3] enter main function */
    addi    sp, sp, -8
    sd      ra, 0(sp)

    call    main

    /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */
    ld      ra, 0(sp)
    addi    sp, sp, 8
    ret

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
