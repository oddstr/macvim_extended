/* code_check.c */
int cc_init __ARGS((void));
int cc_get_bufcount __ARGS((void));
int cc_get_is_started __ARGS((void));
int cc_update_ew_lnums __ARGS((buf_T *buf, int lnum, int col, long xtra));
void cc_update_screen __ARGS((void));
int cc_get_ew_type __ARGS((buf_T *buf, linenr_T lnum));
int cc_request_compile __ARGS((buf_T *buf));
int cc_is_buf_ok __ARGS((buf_T *buf));
int cc_is_buf_watched __ARGS((buf_T *buf));
int cc_addbuf_setcmd __ARGS((buf_T *buf, char_u *cmd));
int cc_rem_buf __ARGS((buf_T *buf));
void cc_exit __ARGS((void));
/* vim: set ft=c : */
