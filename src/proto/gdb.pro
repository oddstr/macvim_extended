/* gdb.c */
gdb_handle_T *gdb_new __ARGS((void));
void gdb_delete __ARGS((gdb_handle_T **));
int gdb_isrunning __ARGS((gdb_handle_T *));
int gdb_fd __ARGS((gdb_handle_T *));
pid_t gdb_pid __ARGS((gdb_handle_T *));
int gdb_isbuffer __ARGS((gdb_handle_T *, buf_T *));
int gdb_allowed __ARGS((gdb_handle_T *));
int gdb_event __ARGS((gdb_handle_T *));
int gdb_sigchld __ARGS((gdb_handle_T *));
void gdb_set_event __ARGS((gdb_handle_T *, int));
void gdb_set_sigchld __ARGS((gdb_handle_T *, int));
int gdb_safe_vgetc __ARGS((gdb_handle_T *));
void gdb_buffer_free __ARGS((gdb_handle_T *, buf_T *));
void gdb_label __ARGS((gdb_handle_T *, buf_T *, char_u *, size_t));
void gdb_docmd __ARGS((gdb_handle_T *, char_u *));
void gdb_setwinput __ARGS((gdb_handle_T *, char_u *));
int gdb_iswinput __ARGS((gdb_handle_T *));
void gdb_winput __ARGS((gdb_handle_T *));
long gdb_process_output __ARGS((gdb_handle_T *, long, void *));
win_T *gdb_window __ARGS((gdb_handle_T *));
/* vim: set ft=c : */
