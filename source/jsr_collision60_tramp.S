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
.macro graffiti name, saved:vararg
.global jsr_coli60_original_graffiti_\name
.type jsr_coli60_original_graffiti_\name, %function
.thumb_func
jsr_coli60_original_graffiti_\name:
    push {\saved}
    cbz r0, .Lreturn_\name
    cbz r1, .Lskip_\name
    movs r3, #0
    tail jsr_coli60_graffiti_\name\()_tail
.Lskip_\name:
    ldr ip, =jsr_coli60_graffiti_\name\()_tail
    ldr ip, [ip]
    add ip, ip, #2
    bx ip
.Lreturn_\name:
    pop {\saved}
    bx lr
.size jsr_coli60_original_graffiti_\name, .-jsr_coli60_original_graffiti_\name
.endm
graffiti pos, r4-r7
graffiti ignore, r4-r7
graffiti range, r4-r6
.global jsr_coli60_original_graffiti_enemy_range
.type jsr_coli60_original_graffiti_enemy_range, %function
.thumb_func
jsr_coli60_original_graffiti_enemy_range:
    push {r4-r7}
    cbz r2, .Lenemy_range_skip
    mov r3, #-1
    tail jsr_coli60_graffiti_enemy_range_tail
.Lenemy_range_skip:
    ldr ip, =jsr_coli60_graffiti_enemy_range_tail
    ldr ip, [ip]
    add ip, ip, #2
    bx ip
.size jsr_coli60_original_graffiti_enemy_range, .-jsr_coli60_original_graffiti_enemy_range
.global jsr_coli60_original_follow
.type jsr_coli60_original_follow, %function
.thumb_func
jsr_coli60_original_follow:
    push {r4-r6}
    cbz r1, .Lfollow_tail
    movs r3, #0
    str r3, [r1]
.Lfollow_tail:
    tail jsr_coli60_follow_tail
.size jsr_coli60_original_follow, .-jsr_coli60_original_follow
.macro static_registration name
.global jsr_coli60_original_\name
.type jsr_coli60_original_\name, %function
.thumb_func
jsr_coli60_original_\name:
    push {r4-r7, lr}
    mov r4, r0
    sub sp, sp, #12
    cbz r1, .Lstatic_skip_\name
    tail jsr_coli60_\name\()_tail
.Lstatic_skip_\name:
    ldr ip, =jsr_coli60_\name\()_tail
    ldr ip, [ip]
    add ip, ip, #4
    bx ip
.size jsr_coli60_original_\name, .-jsr_coli60_original_\name
.endm
static_registration static_object
static_registration ground
.global jsr_coli60_original_simple_move
.type jsr_coli60_original_simple_move, %function
.thumb_func
jsr_coli60_original_simple_move:
    push {r4-r8, lr}
    mov r6, r2
    mov r4, r0
    tail jsr_coli60_simple_move_tail
.size jsr_coli60_original_simple_move, .-jsr_coli60_original_simple_move
.global jsr_coli60_original_enemy_pos
.type jsr_coli60_original_enemy_pos, %function
.thumb_func
jsr_coli60_original_enemy_pos:
    push {r4-r6}
    cbz r0, .Lenemy_pos_return
    ldr r3, =jsr_coli60_enemy_pos_literal
    ldr r3, [r3]
    mov r1, r0
    tail jsr_coli60_enemy_pos_tail
.Lenemy_pos_return:
    pop {r4-r6}
    bx lr
.size jsr_coli60_original_enemy_pos, .-jsr_coli60_original_enemy_pos
.ltorg
.section .note.GNU-stack,"",%progbits
