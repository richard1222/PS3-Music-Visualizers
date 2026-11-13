#ifndef NEON_RUNTIME_PS3_H
#define NEON_RUNTIME_PS3_H
int runtime_init(void);
int runtime_exit_requested(void);
int runtime_draw_suspended(void);
void runtime_shutdown(void);
#ifdef NL_DIAGNOSTIC
void runtime_diagnostic_stall_flip(int exit_during_wait);
void runtime_diagnostic_menu_open(void);
#endif
#endif
