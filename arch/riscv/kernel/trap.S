#include <asm.h>
#include <csr.h>

ENTRY(setup_exception)

  /* TODO: [p2-task3] save exception_handler_entry into STVEC */
  la    t0, exception_handler_entry   
  csrw  CSR_STVEC, t0   # set stvec（Direct mode）

  # enable global interrupt
  call  enable_interrupt

  jr    ra
  /* TODO: [p2-task4] enable interrupts globally */

ENDPROC(setup_exception)
