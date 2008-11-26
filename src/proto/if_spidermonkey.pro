/* if_spidermonkey.c */
int spidermonkey_enabled __ARGS((int verbose));
void spidermonkey_end __ARGS((void));
void ex_spidermonkey __ARGS((exarg_T *eap));
void ex_spidermonkeyfile __ARGS((exarg_T *eap));
void spidermonkey_buffer_free __ARGS((buf_T *buf));
void spidermonkey_window_free __ARGS((win_T *win));
/* vim: set ft=c : */
