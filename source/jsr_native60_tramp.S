.syntax unified
.arch armv8-a
.fpu neon-fp-armv8
.thumb
.text
.align 2

.macro tail symbol
    ldr ip, =\symbol
    ldr ip, [ip]
    bx ip
.endm

.macro original name, bytes:vararg
.global jsr_n60_original_\name
.type jsr_n60_original_\name, %function
.thumb_func
jsr_n60_original_\name:
    .byte \bytes
    tail jsr_n60_\name\()_tail
.size jsr_n60_original_\name, .-jsr_n60_original_\name
.endm

original motion, 0xd1,0xed,0x00,0x7a,0x07,0xee,0x10,0x2a
original motion_ex, 0xd1,0xed,0x00,0x7a,0x07,0xee,0x10,0x2a
original angle, 0x03,0x04,0x09,0x04,0x1b,0x0c,0x08,0x0c
original scalar, 0x06,0xee,0x10,0x0a,0x07,0xee,0x90,0x1a
original speed, 0x70,0xb5,0x2d,0xed,0x02,0x8b,0x04,0x46
original turn, 0x07,0xee,0x90,0x2a,0xb7,0xee,0x00,0x7a
original ride, 0x2d,0xe9,0xf0,0x41,0x04,0x46,0x90,0xb0
original jump, 0x2d,0xe9,0xf0,0x41,0xd0,0xf8,0x34,0x32
original grind, 0x2d,0xe9,0xf0,0x43,0x04,0x46,0x8f,0xb0
original wall, 0xf0,0xb5,0x04,0x46,0x8f,0xb0,0xd0,0xf8,0x8c,0x50
original fall, 0x10,0xb5,0xd0,0xf8,0x04,0x32,0x04,0x46

.global jsr_n60_original_stage
.type jsr_n60_original_stage, %function
.thumb_func
jsr_n60_original_stage:
    push {r4-r7, lr}
    ldr r4, =jsr_n60_stage_literals
    ldr r3, [r4, #4]
    ldr r4, [r4]
    sub sp, sp, #28
    tail jsr_n60_stage_tail
.size jsr_n60_original_stage, .-jsr_n60_original_stage

.macro save_midblock
    push {r0-r3, ip, lr}
    vpush {d0-d15}
    vpush {d16-d31}
    mrs r2, APSR
    vmrs r3, fpscr
    push {r2, r3}
.endm

.macro restore_midblock
    pop {r2, r3}
    vmsr fpscr, r3
    msr APSR_nzcvq, r2
    vpop {d16-d31}
    vpop {d0-d15}
    pop {r0-r3, ip, lr}
.endm

.macro dispatch name
.global jsr_n60_dispatch_\name
.type jsr_n60_dispatch_\name, %function
.thumb_func
jsr_n60_dispatch_\name:
    save_midblock
    mov r0, r4
    mov r1, r9
    bl jsr_n60_dispatch
    restore_midblock
    ldr r3, [r4]
    and r2, r3, #1
    tail jsr_n60_dispatch_\name\()_tail
.size jsr_n60_dispatch_\name, .-jsr_n60_dispatch_\name
.endm

dispatch first
dispatch second

.global jsr_n60_fall_block_entry
.type jsr_n60_fall_block_entry, %function
.thumb_func
jsr_n60_fall_block_entry:
    save_midblock
    bl jsr_n60_fall_block
    restore_midblock
    tail jsr_n60_fall_block_tail
.size jsr_n60_fall_block_entry, .-jsr_n60_fall_block_entry
.ltorg
.section .note.GNU-stack,"",%progbits
