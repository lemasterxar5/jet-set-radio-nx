.syntax unified
.arch armv8-a
.fpu neon-fp-armv8
.thumb
.text
.align 2
.global jsr_shadow_original_polygon
.type jsr_shadow_original_polygon, %function
.thumb_func
jsr_shadow_original_polygon:
    push {r4-r11, lr}
    vpush {d8}
    ldr ip, =jsr_shadow_polygon_tail
    ldr ip, [ip]
    bx ip
.size jsr_shadow_original_polygon, .-jsr_shadow_original_polygon
.ltorg
.section .note.GNU-stack,"",%progbits
