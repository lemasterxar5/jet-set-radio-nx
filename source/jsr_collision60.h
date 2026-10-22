#ifndef JSR_COLLISION60_H
#define JSR_COLLISION60_H

#include "so_util.h"

int jsr_collision60_install(so_module *module);
void jsr_collision60_reset(void);
void jsr_collision60_tick(void);
void jsr_collision60_prepare(void *player);
void jsr_collision60_begin(void *player);
void jsr_collision60_end(void);
void jsr_collision60_forget(void *player);
int jsr_collision60_hold(void *player);

#endif
