.syntax unified
.arch armv8-a
.fpu neon-fp-armv8
.thumb
.text
.align 2
.macro original name, bytes:vararg
.global jsr_pose_original_\name
.type jsr_pose_original_\name, %function
.thumb_func
jsr_pose_original_\name:
    .byte \bytes
    ldr ip, =jsr_pose_\name\()_tail
    ldr ip, [ip]
    bx ip
.size jsr_pose_original_\name, .-jsr_pose_original_\name
.endm
original copy, 0x70,0xb5,0x0c,0x88,0x84,0xb0,0x7f,0x2c
original compose, 0x2d,0xe9,0xf0,0x4f,0x2d,0xed,0x02,0x8b
original draw, 0x2d,0xe9,0xf0,0x47,0x2d,0xed,0x04,0x8b
.ltorg
.section .note.GNU-stack,"",%progbits
