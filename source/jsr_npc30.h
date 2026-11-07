#ifndef JSR_NPC30_H
#define JSR_NPC30_H
#include "so_util.h"
int jsr_npc30_validate(so_module *module);
void jsr_npc30_install(so_module *module);
int jsr_npc30_handles(void (*callback)(void *));
int jsr_npc30_dispatch(void *object, void (*callback)(void *), unsigned ticks);
#endif
