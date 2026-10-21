#ifndef JSR_NATIVE60_H
#define JSR_NATIVE60_H

#include "so_util.h"

int jsr_native60_install(so_module *module);
int jsr_native60_requested(void);
int jsr_native60_begin(void);
void jsr_native60_run(void (*step)(void *, void *), void *env, void *cls);
void jsr_native60_reset(void);

#endif
