.syntax unified
.arch armv8-a
.fpu neon-fp-armv8
.thumb
.text
.align 2
.macro return_tail symbol
    push {ip}
    push {ip}
    ldr ip, =\symbol
    ldr ip, [ip]
    str ip, [sp, #4]
    pop {ip, pc}
.endm
.macro scaled destination, source, operation
    push {ip}
    vpush {d0}
    ldr ip, =jsr_event60_scale
    vldr s0, [ip]
    v\operation\().f32 \destination, \source, s0
    vpop {d0}
    pop {ip}
.endm

.global jsr_event60_camera0
.type jsr_event60_camera0, %function
.thumb_func
jsr_event60_camera0:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    vstr s15, [r4, #124]
    return_tail jsr_event60_camera0_tail
.size jsr_event60_camera0, .-jsr_event60_camera0
.ltorg

.global jsr_event60_camera1
.type jsr_event60_camera1, %function
.thumb_func
jsr_event60_camera1:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    vstr s15, [r4, #124]
    return_tail jsr_event60_camera1_tail
.size jsr_event60_camera1, .-jsr_event60_camera1
.ltorg

.global jsr_event60_camera_reverse
.type jsr_event60_camera_reverse, %function
.thumb_func
jsr_event60_camera_reverse:
    vmov.f32 s15, #1.0
    scaled s15, s14, sub
    return_tail jsr_event60_camera_reverse_tail
.size jsr_event60_camera_reverse, .-jsr_event60_camera_reverse
.ltorg

.global jsr_event60_enemy_root
.type jsr_event60_enemy_root, %function
.thumb_func
jsr_event60_enemy_root:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_enemy_root_tail
.size jsr_event60_enemy_root, .-jsr_event60_enemy_root
.ltorg

.global jsr_event60_enemy_pose
.type jsr_event60_enemy_pose, %function
.thumb_func
jsr_event60_enemy_pose:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_enemy_pose_tail
.size jsr_event60_enemy_pose, .-jsr_event60_enemy_pose
.ltorg

.global jsr_event60_enemy_clip0
.type jsr_event60_enemy_clip0, %function
.thumb_func
jsr_event60_enemy_clip0:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    vstr s15, [r4, #180]
    return_tail jsr_event60_enemy_clip0_tail
.size jsr_event60_enemy_clip0, .-jsr_event60_enemy_clip0
.ltorg

.global jsr_event60_enemy_clip1
.type jsr_event60_enemy_clip1, %function
.thumb_func
jsr_event60_enemy_clip1:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_enemy_clip1_tail
.size jsr_event60_enemy_clip1, .-jsr_event60_enemy_clip1
.ltorg

.global jsr_event60_enemy_clip2
.type jsr_event60_enemy_clip2, %function
.thumb_func
jsr_event60_enemy_clip2:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_enemy_clip2_tail
.size jsr_event60_enemy_clip2, .-jsr_event60_enemy_clip2
.ltorg

.global jsr_event60_object_root
.type jsr_event60_object_root, %function
.thumb_func
jsr_event60_object_root:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_object_root_tail
.size jsr_event60_object_root, .-jsr_event60_object_root
.ltorg

.global jsr_event60_object_pose0
.type jsr_event60_object_pose0, %function
.thumb_func
jsr_event60_object_pose0:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_object_pose0_tail
.size jsr_event60_object_pose0, .-jsr_event60_object_pose0
.ltorg

.global jsr_event60_object_pose1
.type jsr_event60_object_pose1, %function
.thumb_func
jsr_event60_object_pose1:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    vstr s15, [r4, #176]
    return_tail jsr_event60_object_pose1_tail
.size jsr_event60_object_pose1, .-jsr_event60_object_pose1
.ltorg

.global jsr_event60_object_pose2
.type jsr_event60_object_pose2, %function
.thumb_func
jsr_event60_object_pose2:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    ldr r3, [r4, #108]
    vstr s15, [r4, #176]
    return_tail jsr_event60_object_pose2_tail
.size jsr_event60_object_pose2, .-jsr_event60_object_pose2
.ltorg

.global jsr_event60_object_history
.type jsr_event60_object_history, %function
.thumb_func
jsr_event60_object_history:
    vmov.f32 s15, #1.0
    vldr s14, [r4, #196]
    scaled s15, s13, add
    return_tail jsr_event60_object_history_tail
.size jsr_event60_object_history, .-jsr_event60_object_history
.ltorg

.global jsr_event60_player_root
.type jsr_event60_player_root, %function
.thumb_func
jsr_event60_player_root:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_player_root_tail
.size jsr_event60_player_root, .-jsr_event60_player_root
.ltorg

.global jsr_event60_player_pose
.type jsr_event60_player_pose, %function
.thumb_func
jsr_event60_player_pose:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_player_pose_tail
.size jsr_event60_player_pose, .-jsr_event60_player_pose
.ltorg

.global jsr_event60_player_clip
.type jsr_event60_player_clip, %function
.thumb_func
jsr_event60_player_clip:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_player_clip_tail
.size jsr_event60_player_clip, .-jsr_event60_player_clip
.ltorg

.global jsr_event60_player_blend
.type jsr_event60_player_blend, %function
.thumb_func
jsr_event60_player_blend:
    scaled s13, s13, add
    vstr s13, [r4, #212]
    ldrh r3, [r3, #2]
    return_tail jsr_event60_player_blend_tail
.size jsr_event60_player_blend, .-jsr_event60_player_blend
.ltorg

.global jsr_event60_rival_root0
.type jsr_event60_rival_root0, %function
.thumb_func
jsr_event60_rival_root0:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    return_tail jsr_event60_rival_root0_tail
.size jsr_event60_rival_root0, .-jsr_event60_rival_root0
.ltorg

.global jsr_event60_rival_clip
.type jsr_event60_rival_clip, %function
.thumb_func
jsr_event60_rival_clip:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    ldr r0, [r4, #204]
    return_tail jsr_event60_rival_clip_tail
.size jsr_event60_rival_clip, .-jsr_event60_rival_clip
.ltorg

.global jsr_event60_rival_pose
.type jsr_event60_rival_pose, %function
.thumb_func
jsr_event60_rival_pose:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    vstr s15, [r4, #296]
    return_tail jsr_event60_rival_pose_tail
.size jsr_event60_rival_pose, .-jsr_event60_rival_pose
.ltorg

.global jsr_event60_rival_root1
.type jsr_event60_rival_root1, %function
.thumb_func
jsr_event60_rival_root1:
    vmov.f32 s14, #1.0
    scaled s15, s15, add
    vstr s15, [r4, #196]
    return_tail jsr_event60_rival_root1_tail
.size jsr_event60_rival_root1, .-jsr_event60_rival_root1
.ltorg

.global jsr_event60_rival_reverse
.type jsr_event60_rival_reverse, %function
.thumb_func
jsr_event60_rival_reverse:
    vmov.f32 s15, #1.0
    scaled s15, s14, sub
    vcmpe.f32 s15, #0.0
    return_tail jsr_event60_rival_reverse_tail
.size jsr_event60_rival_reverse, .-jsr_event60_rival_reverse
.ltorg

.global jsr_event60_player_sound
.type jsr_event60_player_sound, %function
.thumb_func
jsr_event60_player_sound:
    push {r0, r1}
    mrs r1, APSR
    ldr r0, =jsr_event60_ticks
    ldr r0, [r0]
    cbz r0, 1f
    msr APSR_nzcvq, r1
    pop {r0, r1}
    mov.w lr, #10
    str lr, [sp]
    movs r0, #7
    return_tail jsr_event60_player_sound_tail
1:
    msr APSR_nzcvq, r1
    pop {r0, r1}
    return_tail jsr_event60_sound_skip
.size jsr_event60_player_sound, .-jsr_event60_player_sound
.ltorg
.section .note.GNU-stack,"",%progbits

