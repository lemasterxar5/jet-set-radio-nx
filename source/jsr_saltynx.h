#ifndef JSR_SALTYNX_H
#define JSR_SALTYNX_H

/* Start before engine constructors/EGL. Optional SaltySD connection. */
void jsr_saltynx_init(void);
/* Render-thread capture only; no IPC or shared-memory writes here. */
void jsr_saltynx_record_viewport(unsigned width, unsigned height);
/* Call exactly once after each successful eglSwapBuffers. Surface dimensions
 * are used only when this frame had no eligible observed viewport. */
void jsr_saltynx_present(unsigned width, unsigned height);
void jsr_saltynx_exit(void);

#endif
