#include <asm/biosdef.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc
.equ task_start_sec, 0x502001f8
.equ task_info_loc, 0x50200200

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here tos use BIOS APIs)
.equ bios_func_entry, 0x50150000

.text
.global main

main:
	// fence on all memory and I/O
	fence

	// TODO: [p1-task1] call BIOS to print string "It's bootblock!"
	li		a7, BIOS_PUTSTR
	la 		a0, launch_msg
	call	bios_func_entry

	// TODO: [p1-task2] call BIOS to read kernel in SD card
	li		a7, BIOS_SDREAD
	la		a0, kernel
	li		a1, 4	
	li		a2, 1
	call	bios_func_entry


	// TODO: [p1-task4] load task-related arguments and pass them to kernel

	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	j		kernel

	/************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's a bootloader...\n\r"
launch_msg: .string "It's fengxiyi's bootloader\n\r"

