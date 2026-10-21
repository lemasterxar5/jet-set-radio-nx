.syntax unified
.arch armv8-a
.fpu neon-fp-armv8
.thumb
.text
.align 2

.macro load_global reg, symbol
    ldr \reg, =\symbol
    ldr \reg, [\reg]
.endm

.global jsr_original_exec
.type jsr_original_exec, %function
.thumb_func
jsr_original_exec:
    stmdb sp!, {r3-r11, lr}
    load_global r4, jsr_exec_literal
    load_global ip, jsr_timer_count
    blx ip
    load_global ip, jsr_exec_tail
    bx ip
.size jsr_original_exec, .-jsr_original_exec

.global jsr_original_draw
.type jsr_original_draw, %function
.thumb_func
jsr_original_draw:
    stmdb sp!, {r3-r9, lr}
    load_global r4, jsr_draw_literal
    load_global ip, jsr_timer_count
    blx ip
    load_global ip, jsr_draw_tail
    bx ip
.size jsr_original_draw, .-jsr_original_draw

.global jsr_original_matrix
.type jsr_original_matrix, %function
.thumb_func
jsr_original_matrix:
    push {r4, r5, r7, lr}
    vpush {d8-d15}
    sub sp, sp, #96
    load_global ip, jsr_matrix_tail
    bx ip
.size jsr_original_matrix, .-jsr_original_matrix

.global jsr_original_motion
.type jsr_original_motion, %function
.thumb_func
jsr_original_motion:
    push {r4-r7, lr}
    vpush {d8}
    vmov s16, r2
    load_global ip, jsr_motion_tail
    bx ip
.size jsr_original_motion, .-jsr_original_motion

.global jsr_original_camera
.type jsr_original_camera, %function
.thumb_func
jsr_original_camera:
    stmdb sp!, {r4-r8, lr}
    vpush {d8}
    load_global ip, jsr_camera_tail
    bx ip
.size jsr_original_camera, .-jsr_original_camera

.global jsr_original_view
.type jsr_original_view, %function
.thumb_func
jsr_original_view:
    push {r3-r5, lr}
    load_global r1, jsr_view_literal
    mov r4, r0
    vpush {d8}
    load_global ip, jsr_view_tail
    bx ip
.size jsr_original_view, .-jsr_original_view
.ltorg
.section .note.GNU-stack,"",%progbits
