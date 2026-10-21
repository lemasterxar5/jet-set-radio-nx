.syntax unified
.arch armv8-a
.fpu neon-fp-armv8
.thumb
.text
.align 2
.macro circle index
.global jsr_visual_original_circle\index
.type jsr_visual_original_circle\index, %function
.thumb_func
jsr_visual_original_circle\index:
    vmov s15, r0
    vmul.f32 s15, s15, s15
    ldr ip, =jsr_visual_circle_tails
    ldr ip, [ip, #(4 * \index)]
    bx ip
.size jsr_visual_original_circle\index, .-jsr_visual_original_circle\index
.endm
circle 0
circle 1
circle 2
.ltorg
.section .note.GNU-stack,"",%progbits
