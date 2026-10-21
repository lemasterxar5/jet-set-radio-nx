#ifndef JSR_POSE60_H
#define JSR_POSE60_H

#include "so_util.h"

int jsr_pose60_install(so_module *module);
void jsr_pose60_reset(void);
void jsr_pose60_begin(void *player);
void jsr_pose60_end(void);
void jsr_pose60_phase(void *player, float phase);
void jsr_pose60_forget(void *player);
void jsr_pose60_motion(const void *motion, float frame, float step, int flags, int extended, float start, float end);

#endif
