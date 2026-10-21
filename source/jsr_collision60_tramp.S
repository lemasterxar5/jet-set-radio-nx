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
.global jsr_coli60_original_ride
.type jsr_coli60_original_ride, %function
.thumb_func
jsr_coli60_original_ride:
    push {r3-r7, lr}
    mov r5, r3
    vpush {d8}
    tail jsr_coli60_ride_tail
.size jsr_coli60_original_ride, .-jsr_coli60_original_ride
.global jsr_coli60_original_move
.type jsr_coli60_original_move, %function
.thumb_func
jsr_coli60_original_move:
    push {r4-r8, lr}
    vpush {d8}
    tail jsr_coli60_move_tail
.size jsr_coli60_original_move, .-jsr_coli60_original_move
.global jsr_coli60_original_object
.type jsr_coli60_original_object, %function
.thumb_func
jsr_coli60_original_object:
    push {r4-r8, lr}
    ldr r5, =jsr_coli60_object_literal
    ldr r5, [r5]
    sub sp, sp, #16
    tail jsr_coli60_object_tail
.size jsr_coli60_original_object, .-jsr_coli60_original_object
.ltorg
.section .note.GNU-stack,"",%progbits
