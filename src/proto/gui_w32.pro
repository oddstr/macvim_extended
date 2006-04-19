/* gui_w32.c */
extern void gui_mch_set_blinking __ARGS((long wait, long on, long off));
extern void gui_mch_stop_blink __ARGS((void));
extern void gui_mch_start_blink __ARGS((void));
extern LRESULT WINAPI vim_WindowProc __ARGS((HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam));
extern void gui_mch_new_colors __ARGS((void));
extern void gui_mch_def_colors __ARGS((void));
extern int gui_mch_open __ARGS((void));
extern int gui_mch_get_winpos __ARGS((int *x, int *y));
extern void gui_mch_set_winpos __ARGS((int x, int y));
extern void gui_mch_set_text_area_pos __ARGS((int x, int y, int w, int h));
extern void gui_mch_enable_scrollbar __ARGS((scrollbar_T *sb, int flag));
extern void gui_mch_set_scrollbar_pos __ARGS((scrollbar_T *sb, int x, int y, int w, int h));
extern void gui_mch_create_scrollbar __ARGS((scrollbar_T *sb, int orient));
extern int gui_mch_adjust_charheight __ARGS((void));
extern GuiFont gui_mch_get_font __ARGS((char_u *name, int giveErrorIfMissing));
extern char_u *gui_mch_get_fontname __ARGS((GuiFont font, char_u *name));
extern void gui_mch_free_font __ARGS((GuiFont font));
extern guicolor_T gui_mch_get_color __ARGS((char_u *name));
extern int gui_mch_haskey __ARGS((char_u *name));
extern void gui_mch_beep __ARGS((void));
extern void gui_mch_invert_rectangle __ARGS((int r, int c, int nr, int nc));
extern void gui_mch_iconify __ARGS((void));
extern void gui_mch_draw_hollow_cursor __ARGS((guicolor_T color));
extern void gui_mch_draw_part_cursor __ARGS((int w, int h, guicolor_T color));
extern void gui_mch_update __ARGS((void));
extern int gui_mch_wait_for_chars __ARGS((int wtime));
extern void gui_mch_clear_block __ARGS((int row1, int col1, int row2, int col2));
extern void gui_mch_clear_all __ARGS((void));
extern void gui_mch_enable_menu __ARGS((int flag));
extern void gui_mch_set_menu_pos __ARGS((int x, int y, int w, int h));
extern void gui_mch_menu_hidden __ARGS((vimmenu_T *menu, int hidden));
extern void gui_mch_draw_menubar __ARGS((void));
extern long_u gui_mch_get_rgb __ARGS((guicolor_T pixel));
extern void gui_mch_activate_window __ARGS((void));
extern void gui_mch_show_toolbar __ARGS((int showit));
extern void gui_mch_show_tabline __ARGS((int showit));
extern int gui_mch_showing_tabline __ARGS((void));
extern void gui_mch_update_tabline __ARGS((void));
extern void gui_mch_set_curtab __ARGS((int nr));
extern void ex_simalt __ARGS((exarg_T *eap));
extern void gui_mch_find_dialog __ARGS((exarg_T *eap));
extern void gui_mch_replace_dialog __ARGS((exarg_T *eap));
extern void gui_mch_mousehide __ARGS((int hide));
extern void gui_mch_destroy_scrollbar __ARGS((scrollbar_T *sb));
extern void gui_mch_getmouse __ARGS((int *x, int *y));
extern void gui_mch_setmouse __ARGS((int x, int y));
extern void gui_mch_flash __ARGS((int msec));
extern void gui_mch_delete_lines __ARGS((int row, int num_lines));
extern void gui_mch_insert_lines __ARGS((int row, int num_lines));
extern void gui_mch_exit __ARGS((int rc));
extern int gui_mch_init_font __ARGS((char_u *font_name, int fontset));
extern int gui_mch_maximized __ARGS((void));
extern void gui_mch_newfont __ARGS((void));
extern void gui_mch_settitle __ARGS((char_u *title, char_u *icon));
extern void mch_set_mouse_shape __ARGS((int shape));
extern char_u *gui_mch_browsedir __ARGS((char_u *title, char_u *initdir));
extern char_u *gui_mch_browse __ARGS((int saving, char_u *title, char_u *dflt, char_u *ext, char_u *initdir, char_u *filter));
extern int get_cmd_args __ARGS((char *prog, char *cmdline, char ***argvp, char **tofree));
extern int gui_is_win32s __ARGS((void));
extern void gui_mch_set_parent __ARGS((char *title));
extern void gui_mch_prepare __ARGS((int *argc, char **argv));
extern int gui_mch_init __ARGS((void));
extern void gui_mch_set_shellsize __ARGS((int width, int height, int min_width, int min_height, int base_width, int base_height, int direction));
extern void gui_mch_set_scrollbar_thumb __ARGS((scrollbar_T *sb, long val, long size, long max));
extern void gui_mch_set_font __ARGS((GuiFont font));
extern void gui_mch_set_fg_color __ARGS((guicolor_T color));
extern void gui_mch_set_bg_color __ARGS((guicolor_T color));
extern void gui_mch_set_sp_color __ARGS((guicolor_T color));
extern void im_set_font __ARGS((LOGFONT *lf));
extern void im_set_position __ARGS((int row, int col));
extern void im_set_active __ARGS((int active));
extern int im_get_status __ARGS((void));
extern void gui_mch_draw_string __ARGS((int row, int col, char_u *text, int len, int flags));
extern void gui_mch_flush __ARGS((void));
extern void gui_mch_get_screen_dimensions __ARGS((int *screen_w, int *screen_h));
extern void gui_mch_add_menu __ARGS((vimmenu_T *menu, int pos));
extern void gui_mch_show_popupmenu __ARGS((vimmenu_T *menu));
extern void gui_make_popup __ARGS((char_u *path_name, int mouse_pos));
extern void gui_make_tearoff __ARGS((char_u *path_name));
extern void gui_mch_add_menu_item __ARGS((vimmenu_T *menu, int idx));
extern void gui_mch_destroy_menu __ARGS((vimmenu_T *menu));
extern void gui_mch_menu_grey __ARGS((vimmenu_T *menu, int grey));
extern int gui_mch_dialog __ARGS((int type, char_u *title, char_u *message, char_u *buttons, int dfltbutton, char_u *textfield));
extern void gui_mch_set_foreground __ARGS((void));
extern void gui_mch_drawsign __ARGS((int row, int col, int typenr));
extern void *gui_mch_register_sign __ARGS((char_u *signfile));
extern void gui_mch_destroy_sign __ARGS((void *sign));
extern int multiline_balloon_available __ARGS((void));
extern void gui_mch_disable_beval_area __ARGS((BalloonEval *beval));
extern void gui_mch_enable_beval_area __ARGS((BalloonEval *beval));
extern void gui_mch_post_balloon __ARGS((BalloonEval *beval, char_u *mesg));
extern BalloonEval *gui_mch_create_beval_area __ARGS((void *target, char_u *mesg, void (*mesgCB)(BalloonEval *, int), void *clientData));
extern void gui_mch_destroy_beval_area __ARGS((BalloonEval *beval));
extern void netbeans_draw_multisign_indicator __ARGS((int row));
/* vim: set ft=c : */