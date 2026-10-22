.syntax unified
.arch armv8-a
.fpu neon-fp-armv8
.thumb
.text
.align 2
.global jsr_input_original_camera
.type jsr_input_original_camera, %function
.thumb_func
jsr_input_original_camera:
    push {r3, lr}
    ldr r3, =jsr_input_camera_flag
    ldr r3, [r3]
    ldr r3, [r3]
    ldr ip, =jsr_input_camera_tail
    ldr ip, [ip]
    bx ip
.size jsr_input_original_camera, .-jsr_input_original_camera
.ltorg
.section .note.GNU-stack,"",%progbits
