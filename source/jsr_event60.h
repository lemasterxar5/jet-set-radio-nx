#ifndef JSR_EVENT60_H
#define JSR_EVENT60_H

#include "so_util.h"

int jsr_event60_validate(so_module *module);
void jsr_event60_install(so_module *module);
int jsr_event60_dispatch(void *object, void (*callback)(void *), float scale, unsigned ticks);

#endif

