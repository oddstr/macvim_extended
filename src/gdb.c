/* vi:set ts=8 sts=4 sw=4:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *			gdb support by Xavier de Gaye
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 *
 * $Id$
 */

#include "vim.h"

#ifdef FEAT_GDB	    /* possibly undefined in feature.h */
# include "clewn/obstack.h"
# include "clewn/gdb.h"
# include "clewn/misc.h"

# ifndef FEAT_GUI
#  include "pty.pro"
# endif

# ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
# endif

# ifdef HAVE_FCNTL_H
#  include <fcntl.h>
# endif

# if defined(HAVE_SYS_SELECT_H) && \
	(!defined(HAVE_SYS_TIME_H) || defined(SYS_SELECT_WITH_SYS_TIME))
#  include <sys/select.h>
# endif

# ifndef HAVE_SELECT
#  ifdef HAVE_SYS_POLL_H
#   include <sys/poll.h>
#  else
#   ifdef HAVE_POLL_H
#    include <poll.h>
#   endif
#  endif
# endif

# ifdef HAVE_TERMIOS_H
#  include <termios.h>
# else
#  include <termio.h>
# endif

/* sun's sys/ioctl.h redefines symbols from termio world */
# if defined(HAVE_SYS_IOCTL_H) && !defined(sun)
#  include <sys/ioctl.h>
# endif

# define SCROLLOFF	6	/* min lines around frame highlite */
# define MAXMEM		64L	/* 'maxmem' option */

/* Not allowed after ^O in INS REP mode or from the input-line window */
# define NOT_ALLOWED_MODE (restart_edit != 0 || cmdwin_type != 0)

/* storage for mtrace hooks */
# if defined(GDB_MTRACE) && defined(HAVE_MTRACE)
__ptr_t (*s_malloc) (size_t, const void *);
void (*s_free) (void *, const void *);
__ptr_t (*s_realloc) (void *, size_t, const void *);
# endif	/* GDB_MTRACE */

static int module_state = -1;	/* initial state (not OK, nor FAIL) */
# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
static char_u *inputrc;		/* readline inputrc file name */
# endif

/* The gdb keyword */
typedef struct
{
    int type;
    char_u *keyword;	/* keyword */
    char_u *tail;	/* optional tail */
    regprog_T *regprog;	/* compiled regexp */
} token_T;

static token_T tokens[] = {
    {CMD_DIR,	    (char_u *)"dir",	(char_u *)"ectory",	NULL},
    {CMD_DETACH,    (char_u *)"det",	(char_u *)"ach",	NULL},
    {CMD_SHELL,	    (char_u *)"she",	(char_u *)"ll",		NULL},
    {CMD_STEPI,	    (char_u *)"si",	(char_u *)"",		NULL},
    {CMD_STEPI,	    (char_u *)"stepi",	(char_u *)"",		NULL},
    {CMD_STEPI,	    (char_u *)"ni",	(char_u *)"",		NULL},
    {CMD_STEPI,	    (char_u *)"nexti",	(char_u *)"",		NULL},
    {CMD_EXECF,	    (char_u *)"fil",	(char_u *)"e",		NULL},
    {CMD_EXECF,	    (char_u *)"ex",	(char_u *)"ec-file",	NULL},
    {CMD_EXECF,	    (char_u *)"cor",	(char_u *)"e-file",	NULL},
    {CMD_BREAK,	    (char_u *)"b",	(char_u *)"reak",	NULL},
    {CMD_BREAK,	    (char_u *)"tb",	(char_u *)"reak",	NULL},
    {CMD_BREAK,	    (char_u *)"hb",	(char_u *)"reak",	NULL},
    {CMD_BREAK,	    (char_u *)"thb",	(char_u *)"reak",	NULL},
    {CMD_DISPLAY,   (char_u *)"disp",	(char_u *)"lay",	NULL},
    {CMD_CREATEVAR, (char_u *)"cr",	(char_u *)"eatevar",	NULL},
    {CMD_UP_SILENT, (char_u *)"up-",	(char_u *)"silently",	NULL},
    {CMD_UP,	    (char_u *)"up",	(char_u *)"",		NULL},
    {CMD_DOWN_SILENT,(char_u *)"down-",	(char_u *)"silently",	NULL},
    {CMD_DOWN,	    (char_u *)"do",	(char_u *)"wn",		NULL},
    {CMD_FRAME,	    (char_u *)"f",	(char_u *)"rame",	NULL},
    {CMD_DISABLE,   (char_u *)"disab",	(char_u *)"le",		NULL},
    {CMD_DELETE,    (char_u *)"del",	(char_u *)"ete",	NULL},
    {CMD_SLECT_FRAME,(char_u *)"sel",	(char_u *)"ect-frame",	NULL},
    {CMD_SYMF,	    (char_u *)"sy",	(char_u *)"mbol-file",	NULL},
    {CMD_SYMF,	    (char_u *)"add-sy",	(char_u *)"mbol-file",	NULL},
    {CMD_ANY,	    NULL,	NULL,		NULL}
};

/* The gdb pattern */
typedef struct {
    int id;		/* pattern id */
    char_u *str;	/* string pattern */
    regprog_T *regprog;	/* compiled regexp */
} pattern_T;

static pattern_T patterns[] = {
    {PAT_DIR,		(char_u *)"^\\s*Source directories searched:\\s*\\(.*\\)$", NULL},
    {PAT_CHG_ANNO,	(char_u *)"^\\s*set\\s\\+an\\%[notate]\\s\\+.*$", NULL},
    {PAT_ADD,		(char_u *)"^0x0*\\(\\x\\+\\)\\>", NULL},
    {PAT_PID,		(char_u *)"^\\s*at\\%[tach] \\+\\([0-9]\\+\\) *$", NULL},
# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
    {PAT_SOURCE,	(char_u *)"^\\s*\\([^:]*\\):\\([^:]*\\):[^:]*:[^:]*:0x0*\\(\\x\\+\\)$", NULL},
    {PAT_QUERY,		(char_u *)"^.*(y or n) \\=$", NULL},
    {PAT_YES,		(char_u *)"^\\s*\\(\\<y\\%[es]\\>\\)\\s*$", NULL},
    {PAT_SFILE,		(char_u *)"^Symbols from\\s*\"\\([^\"]\\+\\)", NULL},
    {PAT_BP_CONT,	(char_u *)"^\\s*\\(c\\)\\>\\|^\\s*\\(cont\\)\\%[inue]\\>", NULL},
    {PAT_ASM_FUNC,	(char_u *)"^\\(\\S\\{-1,}\\)\\%((.*)\\)* .*in section .text$", NULL},
    {PAT_ASM_FUNC_P,	(char_u *)"^.*<\\(\\S\\{-1,}\\)\\%(+\\d\\+\\)*>$", NULL},
    {PAT_FRAME,		(char_u *)"^#\\d\\+\\s\\+0x0*\\(\\x\\+\\)\\>", NULL},
# endif
# ifdef GDB_LVL2_SUPPORT
    /* MUST add '.*' at end of each info (possibly last) breakpoint field
     * pattern because GDB adds the hit count, in a new line, after
     * printing the last field and within its annotation context */
    {PAT_BP_ASM,	(char_u *)"^<\\(\\S\\+\\)+\\d\\+>.*$\\|^<\\(\\S\\+\\)>.*$", NULL},
    {PAT_BP_SOURCE,	(char_u *)"^.\\{-}\\(\\S\\+\\):\\(\\d\\+\\).*$", NULL},
    {PAT_DISPLAY,	(char_u *)"^\\s*disp\\%[lay]\\s*$", NULL},
    {PAT_DISPINFO,	(char_u *)"^\\(\\d\\+\\):", NULL},
    {PAT_CREATEVAR,	(char_u *)"^\\s*cr\\%[eatevar]\\>\\s*\\(.*\\)$", NULL},
# endif
# ifdef GDB_LVL3_SUPPORT
    {PAT_CRVAR_FMT,	(char_u *)"^\\s*cr\\%[eatevar]\\>\\s*\\(/[tdxo]\\)\\{,1}\\>\\s*\\(.*\\)$", NULL},
    {PAT_INFO_FRAME,	(char_u *)"^Stack level \\(\\d\\+\\), frame at ", NULL},
# endif
    {0,			NULL, NULL}
};

# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
/* gdb readline inputrc file content */
static char *readline[] = {
    "set show-all-if-ambiguous on\n",
	"set completion-query-items 20\n",	/* max without prompting user */
	"Control-u: unix-line-discard\n",
	NULL
};
# endif

/* Gdb process mgmt */
# define GDB_STATE(i,s) (((i) != NULL) ? ((((gdb_T *)(i))->state) & (s)):FALSE)
static void gdb_abort __ARGS((void));
static int start_gdb_process __ARGS((gdb_T *, char_u *));
static int module_init __ARGS((void));
static void module_end __ARGS((void));
static void clear_gdb_T __ARGS((gdb_T *));
static int exec_gdb __ARGS((gdb_T *));

/* Window and buffer mgmt */
static win_T *ftowin __ARGS((char_u *));
static buf_T *buffer_create __ARGS((char_u *, int, int, int, char_u *));
static void cursor_atbot __ARGS((gdb_T *));
static int line_append __ARGS((gdb_T *, char_u *));

/* Utilities */
static void histdel __ARGS((char_u *));

/** Create gdb, return its handle */
    gdb_handle_T *
gdb_new()
{
    gdb_T *gdb;

# if defined(GDB_MTRACE) && defined(HAVE_MTRACE)
    mtrace();
    mv_hooks;
#endif
    /* register an abort function for when allocating memory fails */
    xatabort(gdb_abort);
    obstack_alloc_failed_handler = gdb_abort;

    gdb = (gdb_T *)xcalloc((unsigned)sizeof(gdb_T));
    gdb->state = GS_INIT;
    gdb->pid   = (pid_t)-1;

    return (gdb_handle_T *)gdb;
}

/** Abort GDB */
    static void
gdb_abort()
{
#if defined(GDB_MTRACE) && defined(HAVE_MTRACE)
    muntrace();
#endif
    fprintf(stderr, "\nvirtual memory exhausted\n");
    mch_exit(1);
}

/** Delete gdb */
    void
gdb_delete(pgdb)
    gdb_handle_T **pgdb;
{
    gdb_T *this;

    if (pgdb == NULL || (this = (gdb_T *)*pgdb) == NULL)
	return;

    if (this->buf != NULL)	/* wipe out the buffer */
	(void)do_bufdel(DOBUF_WIPE, (char_u *)"", 1, 0, this->buf->b_fnum, TRUE);

    gdb_close(this);
    module_end();		/* release module resources */
    clear_gdb_T(this);
    xfree(this);
    *pgdb = NULL;
# if defined(GDB_MTRACE) && defined(HAVE_MTRACE)
    muntrace();
# endif
}

/** Return TRUE when gdb is running */
    int
gdb_isrunning(gdb)
    gdb_handle_T *gdb;
{
    return GDB_STATE(gdb, GS_UP);
}

/** Return gdb pty file descriptor */
    int
gdb_fd(gdb)
    gdb_handle_T *gdb;
{
    return (gdb != NULL ? ((gdb_T *)gdb)->fd : -1);
}

/** Return gdb process id */
    pid_t
gdb_pid(gdb)
    gdb_handle_T *gdb;
{
    return (gdb != NULL ? ((gdb_T *)gdb)->pid : (pid_t)-1);
}

/** Return TRUE when buf is the gdb buffer */
    int
gdb_isbuffer(gdb, buf)
    gdb_handle_T *gdb;
    buf_T *buf;		/* buffer to check for */
{
    return ((gdb != NULL && ((gdb_T *)gdb)->buf != NULL)
	    ? ((gdb_T *)gdb)->buf == buf : FALSE);
}

/** Return TRUE when gdb output may be processed from low level functions */
    int
gdb_allowed(gdb)
    gdb_handle_T *gdb;
{
    return (GDB_STATE(gdb, GS_UP) && GDB_STATE(gdb, GS_ALLOWED));
}

/** Return TRUE when a gdb event is pending */
    int
gdb_event(gdb)
    gdb_handle_T *gdb;
{
    return GDB_STATE(gdb, GS_EVENT);
}

/** Return TRUE when there is a pending SIGCHLD */
    int
gdb_sigchld(gdb)
    gdb_handle_T *gdb;
{
    return GDB_STATE(gdb, GS_SIGCHLD);
}

/** Set or unset a gdb event */
    void
gdb_set_event (gdb, set)
    gdb_handle_T *gdb;
    int set;		/* TRUE when setting event */
{
    if (gdb == NULL)
	return;

    ((gdb_T *)gdb)->state &= ~GS_EVENT;
    if (set)
	((gdb_T *)gdb)->state |= GS_EVENT;
}

/** Set or unset a SIGCHLD event */
    void
gdb_set_sigchld (gdb, set)
    gdb_handle_T *gdb;
    int set;		/* TRUE when setting SIGCHLD event */
{
    if (gdb == NULL)
	return;

    ((gdb_T *)gdb)->state &= ~GS_SIGCHLD;
    if (set)
	((gdb_T *)gdb)->state |= GS_SIGCHLD;
}

/**
 * Entry point for safe_vgetc().
 * Set a flag to prevent recursion and enable gdb to read its pseudo tty
 * in the low level routines.
 * Return safe_vgetc return value.
 */
    int
gdb_safe_vgetc(gdb)
    gdb_handle_T *gdb;
{
    gdb_T *this = (gdb_T *)gdb;
    int s_a;
    int rc;

    if (this == NULL)
	return safe_vgetc();

    s_a = (this->state & GS_ALLOWED);

    /* set state GS_ALLOWED TRUE when called from top level,
     * not in visual mode and not in the input-line window */
    if (this->recurse == 0 && cmdwin_type == 0 && !VIsual_active)
    {
	this->state |= GS_ALLOWED;
	s_a = FALSE;
    }
    else
	this->state &= ~GS_ALLOWED;

    this->recurse++;
    rc = safe_vgetc();	/* call it now */
    this->recurse--;

    this->state &= ~GS_ALLOWED;
    if (s_a)
	this->state |= GS_ALLOWED;
    return rc;
}

/** Take note Vim is freeing a buffer */
    void
gdb_buffer_free(gdb, buf)
    gdb_handle_T *gdb;
    buf_T *buf;		/* buffer being freed */
{
    gdb_T *this = (gdb_T *)gdb;
    bpinfo_T *p, **pt;
    int i;

    if (this != NULL)
    {
	if (this->buf == buf)
	    this->buf = NULL;

	/* variables window buffer */
	else if (this->var_buf == buf)
	{
	    this->var_buf = NULL;

	    /* call mode specifice var_delete */
	    if (this->var_delete != NULL)
		this->var_delete(this);	/* delete all variables */
	}

	/* asm buffers */
	else
	    for (i = 0; i < this->pool.max; i++)
		if (this->pool.buf[i] == buf)
		{
		    this->pool.buf[i] = NULL;
		    break;
		}

	/* remove in bpinfo list the records corresponding to the
	 * signs in buf
	 * the signs themselves are supposed to be removed by Vim
	 * in free_buffer()*/
	for (pt = &(this->bpinfo); *pt != NULL; )
	{
	    p = *pt;
	    if (p->buf == buf)
	    {
		gdb_undefine_sign(p->typenr);
		*pt = p->next;		/* unlink record */
		xfree(p);
	    }
	    else
		pt = &(p->next);
	}
    }
}

# define STL_GDB  "gdb "
# define STL_SEP1 "- "
# define STL_SEP2 " ["
# define STL_TAIL "]"
/** Write gdb status line label in dst */
    void
gdb_label(gdb, buf, dst, len)
    gdb_handle_T *gdb;
    buf_T *buf;		/* stl's buffer */
    char_u *dst;	/* where to write the label */
    size_t len;		/* label max length */
{
    gdb_T *this = (gdb_T *)gdb;
    size_t cnt = 0;
    char_u *fname;
    size_t l;
# define STLCPY(s) {\
	l = (((s)!=NULL) ? STRLEN((s)):0);\
	l = MIN(l,len-cnt-1);	\
	STRNCPY(dst+cnt,(s),l);	\
	cnt+=l;			\
	}

    /* a console window displaying gdb buffer */
    if (this != NULL && this->buf == buf && (this->state & GS_UP)
	    && dst != NULL && len > 0)
    {
	STLCPY(STL_GDB);
	if (this->sfile != NULL)
	{
	    STLCPY(STL_SEP1);
	    fname = gettail(this->sfile);
	    STLCPY(fname);
	}

	STLCPY(STL_SEP2);
	STLCPY(this->status);
	STLCPY(STL_TAIL);
	*(dst + cnt) = NUL;
    }
}

/** Send a cmd to gdb */
    void
gdb_docmd(gdb, cmd)
    gdb_handle_T *gdb;
    char_u  *cmd;	/* gdb cmd */
{
    gdb_T *this = (gdb_T *)gdb;

    if (this == NULL || NOT_ALLOWED_MODE)
	return;

    /* start gdb */
    this->state &= ~GS_STARTED;
    if (!(this->state & GS_UP) && start_gdb_process(this, cmd) == FAIL)
	return;

    /* process first cmd later */
    if (this->state & GS_STARTED)
	return;

    /* pop up gdb console */
    gdb_popup_console(this);

    /* The next statement has the following purpose:
     * When stepping by hitting continuously key 'I', the discarded command
     * give_warning (see few lines below) may be permanently displayed losing
     * thus all meaning: better to clear the cmd line before possibly
     * outputing the msg. */
    msg_clr_cmdline();

    /* accept one cmd at a time, allow intr */
    if (cmd != NULL && *cmd != NUL && *(cmd + STRLEN(cmd) - 1) == KEY_INTERUPT)
	this->oob.state |= OS_INTR;
    else if (this->oob.state & OS_CMD)
    {
	give_warning((char_u *)"GDB busy: command discarded, please retry", TRUE);
	return;
    }
    else
	this->oob.idx = -1;	/* needed when last oob was aborted with OS_QUITs */
    this->oob.state |= OS_CMD;

    /* call mode specific docmd */
    if (this->gdb_docmd != NULL)
	this->gdb_docmd(this, cmd);
}

/** Set the cmd to be inserted later in the input-line window */
    void
gdb_setwinput(gdb, cmd)
    gdb_handle_T *gdb;
    char_u *cmd;	/* cmd to insert */
{
    gdb_T *this = (gdb_T *)gdb;

    if (this == NULL || NOT_ALLOWED_MODE)
	return;

    if (cmd == NULL)
	cmd = (char_u *)"";

    if (STRCHR(cmd, (int)NL) != NULL)	/* assert no NL in cmd */
	return;

    xfree(this->winput_cmd);
    this->winput_cmd = vim_strsave_escaped(cmd, (char_u *)"\"");
}

/** Return TRUE if we are opening the gdb input-line window */
    int
gdb_iswinput(gdb)
    gdb_handle_T *gdb;
{
    return (gdb != NULL ? ((gdb_T *)gdb)->winput_cmd != NULL : FALSE);
}

/** Launch the input-line window */
    void
gdb_winput(gdb)
    gdb_handle_T *gdb;
{
    gdb_T *this = (gdb_T *)gdb;
    int s_silent = cmd_silent;
    char_u *cedit = NULL;
    char_u *cmd = NULL;
    char_u *res = NULL;
    char_u key[2] = {NUL, NUL};
    char_u *trim;
    char_u *p;
    char_u *q = NULL;		/* initialized to please the compiler */
    char_u *r;

    if (this == NULL || NOT_ALLOWED_MODE)
	return;

    if (this->winput_cmd == NULL || !stuff_empty() || typebuf.tb_len != 0)
	goto fin;

    /* pop up gdb console */
    if (this->state & GS_UP)
	gdb_popup_console(this);

    /* insert in typebuf: <C-F> . "a" . winput_cmd */
    /* 'a' start insert mode */
    p = NULL;
    key[0] = Ctrl_F;
    gdb_cat(&p, key);
    gdb_cat(&p, (char_u *)"a");
    gdb_cat(&p, this->winput_cmd);

    if (ins_typebuf(p, REMAP_NONE, 0, TRUE, TRUE) == FAIL)
    {
	xfree(p);
	goto fin;
    }
    xfree(p);

    /* set cedit */
    (void)get_option_value((char_u *)"cedit", NULL, &cedit, OPT_GLOBAL);
    set_option_value((char_u *)"cedit", 0L, key, OPT_GLOBAL);

    /* Launch the input-line window */
    /* careful: must free cmd with vim_free */
    cmd_silent = FALSE;
    cmd = getcmdline_prompt((int)'@', NULL, 0, EXPAND_NOTHING, NULL);
    cmd_silent = s_silent;
    msg_didout = FALSE;

    /* restore cedit */
    if (cedit != NULL)
    {
	set_option_value((char_u *)"cedit", 0L, cedit, OPT_GLOBAL);
	xfree(cedit);
    }
    cursor_atbot(this);

    /* Do not start gdb when interrupt */
    if (!(this->state & GS_UP)
	    && (cmd == NULL || STRCHR(cmd, KEY_INTERUPT) != NULL))
    {
	histdel(cmd);
	goto fin;
    }

    /* User typed ^C^C: send an interrupt */
    if (cmd == NULL)
    {
	key[0] = KEY_INTERUPT;
	gdb_docmd(gdb, key);
    }
    else	/* Cleanup history */
    {
	/* p first word, q first white after last word */
	trim = (char_u *)clewn_strsave((char *)cmd);
	r = p = skipwhite(trim);
	do {
	    q = skiptowhite(r);
	    r = skipwhite(q);
	} while (*r != NUL);

	if ((res = STRCHR(cmd, '\t')) != NULL)
	{
	    histdel(cmd);
	    *(res + 1) = NUL;	/* trim after <Tab> */
	}
	else if ((res = STRCHR(cmd, KEY_INTERUPT)) != NULL)
	{
	    histdel(cmd);
	    *(res + 1) = NUL;	/* trim after ^Z */
	}
	/* trim surrounding spaces for syntax highliting */
	else if (trim != NULL && (p != trim || *q != NUL))
	{
	    histdel(cmd);
	    if (*q != NUL)
		*q = NUL;
	    if (*p != NUL)
		add_to_history(HIST_INPUT, p, FALSE, NUL);
	}
	xfree(trim);

	FREE(this->winput_cmd);

	/* send cmd */
	gdb_docmd(gdb, cmd);

	vim_free(cmd);
	return;
    }
fin:
    vim_free(cmd);
    FREE(this->winput_cmd);
}

/**
 * Invoke parse_output and compute time left to wait for a character in
 * the caller, mch_inchar.
 * Return time left or -1 when mch_inchar must trigger the input-line window.
 */
    long
gdb_process_output(gdb, wtime, start)
    gdb_handle_T *gdb;
    long wtime;		/* time left */
    void *start;	/* start time in mch_inchar */
{
    gdb_T *this = (gdb_T *)gdb;
# if defined(HAVE_GETTIMEOFDAY) && defined(HAVE_SYS_TIME_H)
    struct timeval  * pstart = (struct timeval *)start;
    struct timeval  tv;
# endif
    int rc;

    if (this != NULL && (this->state & GS_ALLOWED))
    {
	if (this->parse_output == NULL)
	    return (wtime > 0 ? wtime : 0L);

	/* call mode specific parser */
	this->state &= ~GS_ALLOWED;	/* prevent recursion */
	rc = this->parse_output(this);
	this->state |= GS_ALLOWED;

	this->state &= ~GS_EVENT;

	if (rc)
	    return -1L;

	if (wtime > 0)
	{
# if defined(HAVE_GETTIMEOFDAY) && defined(HAVE_SYS_TIME_H)
	    /* compute new remaining time */
	    if (pstart != NULL)
	    {
		gettimeofday(&tv, NULL);
		wtime -= (tv.tv_sec - pstart->tv_sec) * 1000L
				+ (tv.tv_usec - pstart->tv_usec) / 1000L;
		pstart->tv_sec = tv.tv_sec;	/* reset start time */
		pstart->tv_usec = tv.tv_usec;
	    }
	    else
# endif
	    {
		/* estimated gdb processing is 10 msecs */
		wtime -= 10L;
	    }
	}
    }
    return (wtime > 0 ? wtime : 0L);
}

/* Start a gdb process; return OK when succcess, FAIL otherwise */
    static int
start_gdb_process(this, cmd)
    gdb_T *this;
    char_u *cmd;	/* the first gdb cmd */
{
    char_u *res = NULL;
    char_u *p;
    int i;

    clear_gdb_T(this);

    if (module_init() == OK && exec_gdb(this) == OK)
    {
	/* save first non empty cmd */
	if (cmd != NULL)
	{
	    p = skipwhite(cmd);
	    if (*p != NUL)
		this->firstcmd = (char_u *)clewn_strsave((char *)cmd);
	}

	/* create variables window buffer */
	if (p_gvar != NULL && *p_gvar != NUL)
	    this->var_buf = buffer_create(p_gvar, TRUE, TRUE, TRUE, (char_u *)"gdbvar");

	/* create pool of asm buffers */
	this->pool.max = MIN(ABS(p_asm), ASM_MAX_BUFF);
	this->pool.idx = 0;
	for (i = 0; i < this->pool.max; i++)
	{
	    gdb_cat(&res, (char_u *)ASM_BUF_NAME);
	    gdb_cat(&res, gdb_itoa(i + 1));

	    this->pool.buf[i] = buffer_create(res, FALSE, FALSE, FALSE, NULL);
	    this->pool.age[i] = ASM_OLD;
	    FREE(res);
	}
	this->pool.last = i + 1;
	cursor_atbot(this);

	this->directories = (char_u *)clewn_strsave(",,");  /* path: current directory */
	return OK;
    }
    return FAIL;
}

/*
 * Initialize this module: set inputrc file, define signs, compile regexp.
 * Return OK when succcess, FAIL otherwise.
 */
    static int
module_init()
{
    char_u *s_cpo = p_cpo;
    pattern_T *pat;
    token_T *tok;
    exarg_T eap;
    char_u *pattern;
    int len;
# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
    char **p;
    int fd;
# endif

    if (module_state == -1)
    {
	module_state = FAIL;	/* do it only once */

# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
	/* Set gdb readline inputrc file contents
	 * careful: must free inputrc with vim_free
	 * We don't know yet if we are going to use GDB/MI, so we need to
	 * setup inputrc just in case, even though it might never be used */
	if ((inputrc = vim_tempname((int)'A')) != NULL
		&& (fd = mch_open((char *)inputrc, O_CREAT | O_RDWR | O_EXTRA, 0644)) >= 0)
	{
	    for (p = readline; *p; p++)
		write(fd, *p, strlen(*p));
	    close(fd);
	    vim_setenv((char_u *)"INPUTRC", inputrc);
	}
# endif

	/* define breakpoint and frame signs */
	do_highlight((char_u *)"gdb_bp term=bold ctermbg=Blue ctermfg=Black guibg=Blue guifg=Black", 0, 0);
	do_highlight((char_u *)"gdb_dbp term=bold ctermbg=Green ctermfg=Black guibg=Green guifg=Black", 0, 0);
	do_highlight((char_u *)"gdb_fr term=bold ctermbg=DarkYellow ctermfg=Black guibg=DarkYellow guifg=Black", 0, 0);

	/* We use for frame and phantom sign type numbers the same
	 * value as their sign numbers. They are respectively FRAME_SIGN
	 * and PHANTOM_SIGN */
	eap.arg = (char_u *)clewn_strsave(
		"define 1 linehl=gdb_fr text==> texthl=Search");
	ex_sign(&eap);
	xfree(eap.arg);

	eap.arg = (char_u *)clewn_strsave(
		"define 2 linehl=Normal text=.. texthl=NonText");
	ex_sign(&eap);
	xfree(eap.arg);

	/* make sure no sign in use */
	buf_delete_all_signs();

	/* Compile patterns */
	p_cpo = (char_u *)"";   /* so that 'l' flag doesn't work */
	for (pat = patterns; pat->str != NULL; pat++)
	    if ((pat->regprog = vim_regcomp(pat->str, TRUE)) == NULL)
		goto fin;

	/* Compile tokens */
	for (tok = tokens; tok->keyword != NULL; tok++)
	{
	    len = (tok->tail != NULL ? STRLEN(tok->tail) : 0);

	    /* pattern: '^\s*\(\<token\%[option]\>\).*$' */
	    pattern = NULL;
	    gdb_cat(&pattern, (char_u *)"^\\s*\\(\\<");
	    gdb_cat(&pattern, tok->keyword);

	    if (len != 0)
	    {
		gdb_cat(&pattern, (char_u *)"\\%[");
		gdb_cat(&pattern, tok->tail);
		gdb_cat(&pattern, (char_u *)"]");
	    }

	    gdb_cat(&pattern, (char_u *)"\\>\\).*$");

	    if ((tok->regprog = vim_regcomp(pattern, TRUE)) == NULL)
	    {
		xfree(pattern);
		goto fin;
	    }
	    xfree(pattern);
	}

	module_state = OK;
    }
fin:
    p_cpo = s_cpo;
    return module_state;
}

/* Release module resources */
    static void
module_end()
{
    pattern_T *pat;
    token_T *tok;
    exarg_T eap;

    module_state = -1;

# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
    if (inputrc != NULL)
    {
	mch_remove(inputrc);
	vim_free(inputrc);
	inputrc = NULL;
    }
# endif

    /* remove signs */
    do_highlight((char_u *)"clear gdb_bp", 0, 0);
    do_highlight((char_u *)"clear gdb_dbp", 0, 0);
    do_highlight((char_u *)"clear gdb_fr", 0, 0);

    /* make sure no sign in use */
    buf_delete_all_signs();

    emsg_skip++;
    eap.arg = (char_u *)clewn_strsave("undefine 1");
    ex_sign(&eap);
    xfree(eap.arg);

    eap.arg = (char_u *)clewn_strsave("undefine 2");
    ex_sign(&eap);
    xfree(eap.arg);
    emsg_skip--;

    /* patterns an tokens */
    for (pat = patterns; pat->str != NULL; pat++)
	if (pat->regprog != NULL)
	    FREE(pat->regprog);

    for (tok = tokens; tok->keyword != NULL; tok++)
	if (tok->regprog != NULL)
	    FREE(tok->regprog);
}

/* Initialize a gdb_T structure */
    static void
clear_gdb_T(this)
    gdb_T *this;
{
    if (this != NULL)
    {
	this->pid = (pid_t)-1;
#if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
	this->height = 0;
#endif
	this->state = GS_INIT;
	FREE(this->status);
	this->recurse = 0;

	this->cmd_type = CMD_ANY;
#if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
	this->cli_cmd.state = CS_START;
	this->cli_cmd.cnt = 0;
	FREE(this->cli_cmd.gdb);
	FREE(this->cli_cmd.readline);
	FREE(this->cli_cmd.echoed);
#endif

	FREE(this->firstcmd);
	FREE(this->winput_cmd);
	FREE(this->directories);
	FREE(this->sfile);

#if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
	this->note = ANO_NONE;
	this->annoted = FALSE;
	this->newline = FALSE;
	FREE(this->annotation);
#endif
	FREE(this->line);
	FREE(this->pc);
	FREE(this->frame_pc);
	FREE(this->oob_result);
	FREE(this->asm_add);
	FREE(this->asm_func);

#if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
	this->bp_state = 0;
	gdb_free_bplist(&(this->tmplist));
#endif
	FREE(this->record);
	this->cont = FALSE;
	gdb_free_bplist(&(this->bpinfo));
	this->frame_curlvl = -1;
	this->frame_lnum = (linenr_T) -1;
	FREE(this->frame_fname);


	this->fr_buf = NULL;

	this->oob.state = 0;
	this->oob.idx = -1;

	this->pool.hilite = FALSE;

	/* free mode specific data within gdb_T */
	if (this->clear_gdb_T != NULL)
	    this->clear_gdb_T(this);

	this->oobfunc = NULL;
	this->parse_output = NULL;
	this->gdb_docmd = NULL;
	this->var_delete = NULL;
	this->clear_gdb_T = NULL;
    }
}

/* Spawn a gdb process; return OK when sucess, FAIL otherwise */
    static int
exec_gdb(this)
    gdb_T *this;
{
    char_u *err = NULL;
    int fd = -1;	/* slave pty file descriptor */
    char *tty;		/* pty name */
    buf_T *buf;
# ifdef HAVE_TERMIOS_H
    struct termios tio;
# else
    struct termio tio;
# endif

    /* process already running */
    if (this->pid != (pid_t)-1 && waitpid(this->pid, NULL, WNOHANG) == 0)
	return OK;

    /* Open pty */
    if ((this->fd = OpenPTY(&tty)) < 0
	    || (fd = open(tty, O_RDWR|O_NOCTTY|O_EXTRA, 0)) < 0
	    || SetupSlavePTY(fd) == -1)
    {
	err = (char_u *)"Cannot open gdb pty";
	goto err;
    }

    /* Set terminal attributes */
# ifdef HAVE_TERMIOS_H
    if (tcgetattr(fd, &tio) == 0)
# else
    if (ioctl(fd, TCGETA, &tio) >= 0)
# endif
    {
	tio.c_oflag &= ~ONLCR;		/* don't map NL to CR-NL on output */
	tio.c_cc[VINTR] = KEY_INTERUPT;
# ifdef HAVE_TERMIOS_H
	if (tcsetattr(fd, TCSAFLUSH, &tio) != 0)
# else
	if (ioctl(fd, TCSETA, &tio) < 0)
# endif
	{
	    err = (char_u *)"Cannot set gdb pty";
	    goto err;
	}
    }
    else
    {
	err = (char_u *)"Cannot get gdb pty";
	goto err;
    }

# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
#  if defined(TIOCGWINSZ) && defined(TIOCSWINSZ)
	{
	    struct winsize win;

	    /* set tty height */
	    if (ioctl(fd, TIOCGWINSZ, &win) >= 0)
	    {
		win.ws_row = LPP_LINES;
		if (ioctl(fd, TIOCSWINSZ, &win) >= 0)
		    this->height = LPP_LINES;
	    }
	}
#  endif
# endif

    /* Fork */
    if ((this->pid = fork()) == (pid_t)-1)
    {
	err = (char_u *)"Cannot fork gdb";
	goto err;
    }
/* The child */
    else if (this->pid == (pid_t)0)
    {
	/* Grab control of terminal (from `The GNU C Library' (glibc-2.3.1)) */
	setsid();
# ifdef TIOCSCTTY
	if (ioctl(fd, TIOCSCTTY, (char *)NULL) == -1)
	    _exit(1);
# else
	{ int newfd;
	char *fdname = ttyname(fd);

	/* This might work (it does on Linux) */
	if (fdname)
	{
	    if (fd != 0)
		close (0);
	    if (fd != 1)
		close (1);
	    if (fd != 2)
		close (2);
	    newfd = open(fdname, O_RDWR);
	    close(newfd);
	}
	}
# endif

	close(0); dup(fd);
	close(1); dup(fd);
	close(2); dup(fd);

	if (fd > 2)
	    close(fd);

	close(this->fd);

	/* close all swap files: on Cygwin, Windows fail to unlink the swap
	 * files while there is still an open file descriptor held by
	 * another (child) process */
	for (buf = firstbuf; buf != NULL; buf = buf->b_next)
	    if (buf->b_ml.ml_mfp != NULL
		    && buf->b_ml.ml_mfp->mf_fd >= 0)
		close(buf->b_ml.ml_mfp->mf_fd);

# ifdef GDB_MI_SUPPORT
	if (p_gdbmi)
	{
	    /* MI mi2 is available starting with GDB 6.0 */
	    execlp(p_gdp, p_gdp, "--interpreter=mi2", NULL);
	    _exit(EXIT_FAILURE);
	}
# endif
# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
	clewn_exec((char *)p_gdp);
# endif /* defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT) */

	_exit(EXIT_FAILURE);
    }
/* The parent */
    else
    {
	close(fd);
	this->state |= GS_UP;
	this->state |= GS_STARTED;

# ifdef GDB_MI_SUPPORT
	if (p_gdbmi)
	{
	    if (gdb_setup_mi(this) != OK)
	    {
		gdb_cat(&err, (char_u *)"Cannot start GDB program \"");
		gdb_cat(&err, p_gdp);
		gdb_cat(&err, (char_u *)"\" (MI)");
		if (err != NULL)
		{
		    EMSG(_(err));
		    xfree(err);
		}
		this->state = GS_INIT;
		return FAIL;
	    }
	    else
		return OK;
	}
# endif
# if defined(GDB_LVL2_SUPPORT) || defined(GDB_LVL3_SUPPORT)
	if (gdb_setup_cli(this) != OK)
	{
	    this->state = GS_INIT;
	    return FAIL;
	}
	else
	    return OK;
# endif

	this->state = GS_INIT;
	return FAIL;
    }
err:
    if (this->fd >= 0)
	close(this->fd);
    if (fd >= 0)
	close(fd);
    if (err != NULL)
	EMSG(_(err));
    return FAIL;
}

# define CG_QUIT "quit\n"
# define CG_YES  "yes\n"
# define CG_TERM "Terminated\n\n"
# define CG_SEP  "####################################\n"
# define CG_POLL 100
/* Close gdb process */
    void
gdb_close(this)
    gdb_T *this;
{
    pid_t pid;
    int i;
    int rc;

    if (this->state & GS_CLOSING)	/* prevent recursive calls */
	return;
    this->state |= GS_CLOSING;

    this->syntax = TRUE;		/* force syntax highlite */
    gdb_write_buf(this, (char_u *)CG_TERM, TRUE);
    gdb_write_buf(this, (char_u *)CG_SEP, TRUE);
    this->syntax = FALSE;

    if (this->buf != NULL)
	close_windows(this->buf, FALSE);

    buf_delete_all_signs();

    /* wipe out variables window buffer */
    if (this->var_buf != NULL)
	(void)do_bufdel(DOBUF_WIPE, (char_u *)"", 1, 0, this->var_buf->b_fnum, TRUE);

    /* wipe out asm buffers */
    for (i = 0; i < this->pool.max; i++)
	if (this->pool.buf[i] != NULL)
	    (void)do_bufdel(DOBUF_WIPE, (char_u *)"", 1, 0, this->pool.buf[i]->b_fnum, TRUE);

    /* free breakpoints table */
    gdb_free_bplist(&(this->bpinfo));

    /*  a) attempt to gracefully terminate gdb process
     *  b) if this fails, SIGTERM it
     *  c) if this fails, too bad, just return */
    if (this->pid != (pid_t)-1)
    {
	pid = waitpid(this->pid, NULL, WNOHANG);

	if ((pid == (pid_t)-1 && errno == ECHILD) || pid == this->pid)
	    close(this->fd);
	else	/* still running */
	{
	    char c     = KEY_INTERUPT;
	    int killed = FALSE;
	    char_u buf[MAX_BUFFSIZE];
	    int t;

	    /* a) write an interrupt followed by a 'quit' cmd */
	    write(this->fd, &c, 1);
	    if (gdb_read(this, buf, MAX_BUFFSIZE, 1000) >= 0)
	    {
		write(this->fd, CG_QUIT, strlen(CG_QUIT));
		while ((rc = gdb_read(this, buf, MAX_BUFFSIZE, 100)) > 0)
		    ;

		if (rc != -1)
		    write(this->fd, CG_YES, strlen(CG_YES));
	    }
	    close(this->fd);

	    /* make sure gdb is terminated: poll for waitpid() */
	    for (t = 0; !killed; t += CG_POLL)
	    {
		/* 1 second elapsed since start of polling for waitpid */
		if (t >= 1000 )
		{
# ifdef SIGTERM
		    /* b) kill it now */
		    kill(this->pid, SIGTERM);
# endif
		    killed = TRUE;
		}

		mch_delay(CG_POLL, TRUE);
		pid = waitpid(this->pid, NULL, WNOHANG);
		if ((pid == (pid_t)-1 && errno == ECHILD) || pid == this->pid)
		    break;
	    }
	}
	MSG_ATTR(_("GDB terminated"), hl_attr(HLF_E));
    }

    gdb_redraw(curbuf);	/* force redrawing */
    this->state = GS_INIT;
    this->pid = (pid_t)-1;
}

/* Highlite asm_add line; return TRUE when asm_add found in asm buffer */
    int
gdb_as_frset(this, obs)
    gdb_T *this;
    struct obstack *obs;
{
    char_u *ptrn = NULL;
    int first = this->pool.idx;
    pos_T pos;
    win_T *win;
    int i;

    if (this->asm_func == NULL)
	return FALSE;

    /* age asm buffers */
    for (i = 0; i < this->pool.max; i++)
	if (this->pool.age[i] < ASM_OLD)
	    this->pool.age[i]++;

    if (this->asm_add != NULL)
    {
	obstack_strcat(obs, "^\\s*0x0*");
	obstack_strcat0(obs, this->asm_add);
	ptrn = (char_u *)obstack_finish(obs);
    }

    if (ptrn != NULL)
    {
	/* Search all asm buffers whose name start with this->asm_func
	 * for ptrn.
	 * Start with the current one. */
	i = first;
	do
	{
	    pos.lnum = 1;
	    pos.col = 0;

	    if (this->pool.buf[i] != NULL
		    && STRSTR(this->pool.buf[i]->b_fname, this->asm_func) != NULL
		    && !(this->pool.buf[i]->b_ml.ml_flags & ML_EMPTY)
		    && searchit(NULL, this->pool.buf[i], &pos,
			FORWARD, ptrn, 1L, SEARCH_KEEP, RE_LAST, (linenr_T)0, NULL) != FAIL)
	    {
		this->pool.idx = i;
		this->pool.age[i] = 0;
		this->pool.lnum = pos.lnum;

		if (this->pool.hilite)
		    gdb_fr_set(this, NULL, NULL, obs);
		else if ((win = gdb_btowin(this->pool.buf[i])) != NULL)
		    gdb_set_cursor(win, pos.lnum);

		FREE(this->asm_add);
		return TRUE;
	    }
	} while ((i = ++i % this->pool.max) != first);

    }
    return FALSE;
}

/*
 * Highlight line within frame
 * Return -1 when failing to load the buffer, 0 otherwise
 */
    int
gdb_fr_set(this, file, line, obs)
    gdb_T *this;
    char_u *file;
    linenr_T *line;
    struct obstack *obs;
{
    win_T *oldwin = curwin;
    buf_T *buf = NULL;
    linenr_T lnum;

    /* Do not set frame hilite when this breakpoint has a 'commands'
     * with a 'continue' statement */
    if (this->cont)
    {
	this->cont = FALSE;
	return 0;
    }

    if (line == NULL)		/* in asm window */
    {
	buf = this->pool.buf[this->pool.idx];
	lnum = this->pool.lnum;
    }
    else			/* in source file */
	lnum = *line;

    if (buf != NULL || file != NULL)
    {
	if (gdb_edit_file(this, buf, file, lnum, obs) != NULL)
	{
	    /* MUST redraw the screen before calling update_debug_sign():
	     *	update_debug_sign() invokes win_update()
	     *	the screen might have been scrolled when Vim ask the
	     *	user to confirm changes made to the previous buffer */
	    gdb_redraw(curwin->w_buffer);

	    gdb_fr_lite(this, curwin->w_buffer, lnum, obs);
	}
	else {
	    win_goto(oldwin);
	    return -1;
	}
    }
    return 0;
}

/* Highlite frame */
    void
gdb_fr_lite(this, buf, lnum, obs)
    gdb_T *this;
    buf_T *buf;		/* where to highlite */
    linenr_T lnum;	/* line number */
    struct obstack *obs;
{
    buf_T *disped = NULL;

    if (obs) {}	    /* keep compiler happy */

    if (buf == NULL || lnum <= 0)
	return;

    /*
     * Remove previous frame sign:
     * GDB sends ANO_FRAME_INVALID annotations whenever stepping, running, etc...
     * and these annotations invoke gdb_fr_unlite() that turn off the previous frame sign.
     * But when moving along the stack frame with GDB 'up', 'down', 'frame' commands,
     * we don't get annotations and must turn off the previous frame sign.
     */
    if (this->fr_buf != NULL)
	disped = gdb_unlite(FRAME_SIGN);

    /* add new frame highlite */
    buf_addsign(buf, FRAME_SIGN, lnum, FRAME_SIGN);
    update_debug_sign(buf, lnum);

    /* remove phantom highlite */
    disped = gdb_unlite(PHANTOM_SIGN);

    this->fr_buf = buf;
    if (gdb_btowin(buf) != NULL)
	disped = buf;

    gdb_redraw(disped);	/* only when displayed */
}

/* Unlite frame */
    void
gdb_fr_unlite(this)
    gdb_T *this;
{
    buf_T *disped = NULL;
    buf_T *buf = this->fr_buf;
    linenr_T lnum;

    if (buf == NULL)
	return;
    this->fr_buf = NULL;

    /* To avoid screen blinks: replace with phantom sign */
    if (gdb_btowin(buf) != NULL && (lnum = BUFLASTL(buf)) > 0)
    {
	buf_addsign(buf, PHANTOM_SIGN, lnum, PHANTOM_SIGN);
	update_debug_sign(buf, lnum);
    }

    disped = gdb_unlite(FRAME_SIGN);
    gdb_redraw(disped);	/* only when displayed */
}

/*
 * Unlite a sign.
 * Return last displayed buffer that contained this sign.
 */
    buf_T *
gdb_unlite(id)
    int id;		/* sign id */
{
    buf_T *disped = NULL;
    buf_T *buf;
    linenr_T lnum;

    for (buf = firstbuf; buf != NULL; buf = buf->b_next)
	if ((lnum = buf_delsign(buf, id)) != 0)
	{
	    update_debug_sign(buf, lnum);
	    if (gdb_btowin(buf) != NULL)
		disped = buf;
	}
    return disped;
}

/*
 * Define a breakpoint sign. There is one sign type per breakpoint
 * sign in order to have breakpoints numbers as the sign text.
 * Returns sign type number or -1 if error.
 */
    int
gdb_define_sign(id, enabled)
    int id;		/* breakpoint number */
    int enabled;	/* TRUE when breakpoint is enabled */
{
    exarg_T eap;
    char_u text[NUMBUFLEN];
    int r;

    if (id <= 0)
	return -1;

    /* the sign text is two chars max */
    if (id < 100)
	STRCPY(text, gdb_itoa(id));
    else
    {
	if ((r = id % 100) < 10)
	{
	    text[0] = '0';
	    STRCPY(text + 1, gdb_itoa(r));
	}
	else
	    STRCPY(text, gdb_itoa(r));
    }

    /* With id = 1 and enabled TRUE, build the following:
     * "define 3 linehl=gdb_bp text=1 texthl=LineNr" */
    eap.arg = NULL;
    gdb_cat(&(eap.arg), (char_u *)"define ");
    gdb_cat(&(eap.arg), gdb_itoa(BP_SIGN_ID(id)));
    gdb_cat(&(eap.arg), (char_u *)" linehl=");

    if (enabled)
	gdb_cat(&(eap.arg), (char_u *)"gdb_bp");
    else
	gdb_cat(&(eap.arg), (char_u *)"gdb_dbp");

    gdb_cat(&(eap.arg), (char_u *)" text=");
    gdb_cat(&(eap.arg), text);
    gdb_cat(&(eap.arg), (char_u *)" texthl=LineNr");

    ex_sign(&eap);
    xfree(eap.arg);
    return BP_SIGN_ID(id);
}

/* Undefine a breakpoint sign */
    void
gdb_undefine_sign(typenr)
    int typenr;		/* sign type number to undefine */
{
    exarg_T eap;

    if (typenr <= 0)
	return;

    emsg_skip++;
    eap.arg = NULL;
    gdb_cat(&(eap.arg), (char_u *)"undefine ");
    gdb_cat(&(eap.arg), gdb_itoa(typenr));
    ex_sign(&eap);
    xfree(eap.arg);
    emsg_skip--;
}

/** Return the (first) console window where is displayed gdb */
    win_T *
gdb_window(gdb)
    gdb_handle_T *gdb;
{
    return (gdb != NULL ? gdb_btowin(((gdb_T *)gdb)->buf): NULL);
}

/* Return (first) window where buf is displayed */
    win_T *
gdb_btowin(buf)
    buf_T *buf;
{
    win_T *win;

    if (buf != NULL)
	for (win = firstwin; win != NULL; win = win->w_next)
	    if (win->w_buffer == buf)
		return win;
    return NULL;
}

/* Return (first) window where file fname is displayed */
    static win_T *
ftowin(fname)
    char_u *fname;
{
    buf_T *buf = NULL;
    char_u *name;

    /* first make the name into a full path name
     * force expansion, get rid of symbolic links */
    if (fname != NULL && (name = FullName_save(fname, TRUE)) != NULL)
    {
	buf = buflist_findname(name);
	xfree(name);
    }
    return gdb_btowin(buf);
}

/* Pop up gdb console, load the buffer when first time */
    void
gdb_popup_console(this)
    gdb_T *this;
{
    win_T *oldwin = curwin;
    char_u *name  = NULL;
    long s_mm     = p_mm;
    long splitbelow;
    long splitright;
    int flags;

    /* already displayed */
    if (this->buf != NULL && gdb_btowin(this->buf) != NULL)
	return;

    /* get a unique name */
    if (this->buf == NULL &&
	    (! (this->state & GS_UP)
	     || (name = vim_tempname((int)'A')) == NULL))
	return;

    /* get splitbelow and splitright options values */
    (void) get_option_value((char_u *)"sb", &splitbelow, NULL, OPT_LOCAL);
    (void) get_option_value((char_u *)"spr", &splitright, NULL, OPT_LOCAL);

    if (splitbelow) {
	if (splitright)
	    flags = WSP_BOT;		    /* bottom */
	else
	    flags = WSP_VERT | WSP_TOP;	    /* left */
    }
    else {
	if (splitright)
	    flags = WSP_VERT | WSP_BOT;	    /* right */
	else
	    flags = WSP_TOP;		    /* the default: top */
    }

    /* create new window */
    if (win_split(p_pvh > 0 ? p_pvh : 0, flags) == FAIL)
    {
	vim_free(name);	    /* free name with vim_free */
	return;
    }

    if (this->buf == NULL)
    {
	p_mm = MAXMEM;	/* buffer max virtual memory */
	(void)do_ecmd(0, name, NULL, NULL, ECMD_ONE, ECMD_HIDE);
	p_mm = s_mm;

	vim_free(name);			/* free name with vim_free */
	msg_clr_cmdline();
	curwin->w_alt_fnum = 0;		/* no alternate */
	this->buf = curbuf;

	/* set buffer local options
	 * wrap, linebreak, nomodifiable, nolist
	 * filetype=gdbvim - bufhidden=hide - buftype=nowrite */
	set_option_value((char_u *)"wrap", 0L, NULL, OPT_LOCAL);
	set_option_value((char_u *)"lbr", 1L, NULL, OPT_LOCAL);
	set_option_value((char_u *)"ma", 0L, NULL, OPT_LOCAL);
	set_option_value((char_u *)"list", 0L, NULL, OPT_LOCAL);
	set_option_value((char_u *)"syn", 0L, (char_u *)"gdbvim", OPT_LOCAL);
	set_option_value((char_u *)"bh", 0L, (char_u *)"hide", OPT_LOCAL);
	set_option_value((char_u *)"bt", 0L, (char_u *)"nowrite", OPT_LOCAL);
	set_option_value((char_u *)"bl", 0L, NULL, OPT_LOCAL);
    }
    else if (this->buf != curbuf)
    {
	set_curbuf(this->buf, DOBUF_GOTO);

	/* nobuflisted must be set each time it's displayed */
	set_option_value((char_u *)"bl", 0L, NULL, OPT_LOCAL);
    }

    /* return to previous window */
    if (win_valid(oldwin))
    {
	prevwin = oldwin;
	win_goto(oldwin);
    }
}

/* Create an assembly buffer */
    static buf_T *
buffer_create(name, wrap, modifiable, listed, syntax)
    char_u *name;	/* buffer name */
    int wrap;		/* TRUE when option 'wrap' is set */
    int modifiable;	/* TRUE when option 'modifiable' is set */
    int listed;		/* TRUE when option 'listed' is set */
    char_u *syntax;	/* buffer syntax */
{
    win_T *oldwin = curwin;
    buf_T *buf = NULL;
    win_T *wp;
    garray_T sizes;

    /* save window sizes */
    win_size_save(&sizes);

# ifdef FEAT_AUTOCMD
    ++autocmd_busy;
# endif
    /* create a tmp window */
    if (win_split(0, WSP_BOT) == OK)
    {
	/* create the asm buffer empty */
	(void)do_ecmd(0, NULL, NULL, NULL, ECMD_ONE, ECMD_HIDE);
	gdb_as_setname(name);
# ifdef FEAT_AUTOCMD
	/* allow autocommands for loading syntax */
	--autocmd_busy;
# endif
	set_option_value((char_u *)"wrap", (long)wrap, NULL, OPT_LOCAL);
	set_option_value((char_u *)"ma", (long)modifiable, NULL, OPT_LOCAL);
	set_option_value((char_u *)"bl", (long)listed, NULL, OPT_LOCAL);

	if (syntax != NULL)
	    set_option_value((char_u *)"syn", 0L, syntax, OPT_LOCAL);

	set_option_value((char_u *)"bt", 0L, (char_u *)"nofile", OPT_LOCAL);
	set_option_value((char_u *)"swf", 0L, NULL, OPT_LOCAL);
	set_option_value((char_u *)"list", 0L, NULL, OPT_LOCAL);
	set_option_value((char_u *)"bh", 0L, (char_u *)"hide", OPT_LOCAL);
# ifdef FEAT_AUTOCMD
	++autocmd_busy;
# endif
	if (curwin != oldwin)
	{
	    buf = curbuf;
	    wp = curwin;
	    win_goto(oldwin);
	    win_close(wp, FALSE);
	}
    }
# ifdef FEAT_AUTOCMD
    --autocmd_busy;
# endif
    win_size_restore(&sizes);
    ga_clear(&sizes);
    return buf;
}

/* Set a unique buffer name */
    void
gdb_as_setname(name)
    char_u *name;	/* buffer name */
{
    char_u *res = NULL;
    int s_t = p_title;
    int s_i = p_icon;
    int i = 0;
    char_u *buf;

    p_title = p_icon = FALSE;
    do
    {
	buf = gdb_itoa(i);
	if (i == 0)
	    *buf = NUL;
	i++;

	FREE(res);
	gdb_cat(&res, name);
	gdb_cat(&res, buf);
    } while (setfname(curbuf, res, NULL, FALSE) == FAIL);

    xfree(res);
    p_title = s_t;
    p_icon  = s_i;
}

/*
 * Clear an asm buffer of its content and signs.
 * Rename it to an asm generic name.
 */
    void
gdb_clear_asmbuf(this, buf)
    gdb_T *this;
    buf_T *buf;		/* asm buffer to clear */
{
    buf_T *oldbuf = curbuf;
    char_u *res   = NULL;
    bpinfo_T *p, **pt;
    linenr_T lnum;

    if (buf == NULL)
	return;

    curbuf = buf;

    /* rename buffer if its name is not a generic name */
    if ((buf->b_sfname == NULL || STRSTR(buf->b_sfname, ASM_BUF_NAME) != buf->b_sfname))
    {
	gdb_cat(&res, (char_u *)ASM_BUF_NAME);
	gdb_cat(&res, gdb_itoa(this->pool.last++));
	gdb_as_setname(res);
	FREE(res);
    }

    lnum = BUFLASTL(buf);
    while (lnum-- > 0)	/* clear buffer */
	ml_delete(buf->b_ml.ml_line_count, FALSE);
    curbuf = oldbuf;

    /* remove all the buffer signs:
     *	in buf
     *	their corresponding bpinfo record
     *	their corresponding sign type */
    for (pt = &(this->bpinfo); *pt != NULL; )
    {
	p = *pt;
	if (p->buf == buf)
	{
	    buf_delsign(buf, BP_SIGN_ID(p->id));
	    gdb_undefine_sign(p->typenr);
	    *pt = p->next;		/* unlink record */
	    xfree(p);
	}
	else
	    pt = &(p->next);
    }

    /* remove frame sign if present */
    if (this->fr_buf == buf)
    {
	gdb_unlite(FRAME_SIGN);
	this->fr_buf = NULL;
    }
}

/* Put back gdb console's last line at bottom. */
/* needed after win_close messes up gdb topline when 'equalalways' on */
    static void
cursor_atbot(this)
    gdb_T *this;
{
    win_T *oldwin = curwin;
    win_T *win;

    if ((win = gdb_btowin(this->buf)) != NULL)
    {
	curwin = win;
	curbuf = curwin->w_buffer;
	scroll_cursor_bot(0, TRUE);
	redraw_later(VALID);
	curwin = oldwin;
	curbuf = curwin->w_buffer;
    }
}

/* Append line to gdb buffer */
    static int
line_append(this, line)
    gdb_T *this;
    char_u *line;	/* line to append */
{
    linenr_T lnum = BUFLASTL(this->buf);
    char_u *res = NULL;
    win_T *win;
    int rc;

# define LN_PREFIX   "  "
    /* disable syntax highliting by prefixing with LN_PREFIX */
    if (! this->syntax)
    {
	gdb_cat(&res, (char_u *)LN_PREFIX);
	gdb_cat(&res, line);
	if ((line = res) == NULL)
	    return FAIL;
    }

    /* first line ever: remove empty line after the one just inserted */
    if ((rc = ml_append(lnum, line, 0, 0)) == OK && lnum == 0)
	ml_delete(this->buf->b_ml.ml_line_count, FALSE);

    if ((win = gdb_btowin(this->buf)) != NULL)
	redraw_win_later(win, NOT_VALID);

    xfree(res);
    return rc;
}

/* Append to/Replace last line in gdb buffer */
    void
gdb_write_buf(this, chunk, add)
    gdb_T *this;
    char_u *chunk;	/* a chunk may contain one, many or no NL */
    int add;		/* TRUE when chunk is added */
{
    win_T *oldwin = curwin;
    win_T *win;
    char_u *start;
    char_u *ptr;
    int cnt;		    /* number of lines appended */

    if (chunk == NULL || this->buf == NULL)
	return;

    /* make a copy */
    chunk = (char_u *)clewn_strsave((char *)chunk);

    /* set curbuf to buf */
    curbuf = this->buf;

    /* delete last line */
    if (!add)
	ml_delete(this->buf->b_ml.ml_line_count, FALSE);

    /* append to buffer line after line */
    cnt = 0;
    for (ptr = start = chunk; *ptr != NUL; ptr++)
    {
	if (*ptr == NL)
	{
	    *ptr = NUL;
	    line_append(this, start);
	    start = ptr + 1;
	    cnt++;
	}
    }

    /* handle an empty line or last non NL terminated stuff */
    if (start == chunk || *start != NUL)
    {
	line_append(this, start);
	cnt++;
    }

    changed_lines(this->buf->b_ml.ml_line_count - cnt, 0,
	    this->buf->b_ml.ml_line_count, cnt);

    /* window displayed */
    if ((win = gdb_btowin(this->buf)) != NULL)
    {
	/* update top line */
	curwin = win;
	curwin->w_cursor.lnum = this->buf->b_ml.ml_line_count;
	update_topline();

	/* status line changed */
	curwin->w_redr_status = TRUE;
    }

    curwin = oldwin;
    curbuf = curwin->w_buffer;
    xfree(chunk);
}

/*
 * Edit a file in a non-console window.
 * Use buf if non NULL, otherwise fname using gdb source path to find the file.
 * Return NULL if error, curwin otherwise.
 */
    win_T *
gdb_edit_file(this, buf, fname, lnum, obs)
    gdb_T *this;
    buf_T *buf;		/* asm buffer to load */
    char_u *fname;	/* file name */
    linenr_T lnum;	/* line number */
    struct obstack *obs;
{
#ifdef GDB_LVL3_SUPPORT
    char_u * source_cur = this->lvl3.source_cur;    /* GDB current source */
    char_u * source_list = this->lvl3.source_list;  /* GDB source list */
#else
    char_u * source_cur = NULL;
    char_u * source_list = NULL;
#endif
    char_u *fpn;
    win_T *win;
    int i;

    if (obs) {}	    /* keep comiler happy */

    if (buf == NULL && (fname == NULL || *fname == NUL))
	return NULL;

#ifdef FEAT_GUI
    need_mouse_correct = TRUE;
#endif

    /* found a window where source is already displayed */
    if ((win = gdb_btowin(buf)) != NULL || (win = ftowin(fname)) != NULL)
    {
	gdb_set_cursor(win, lnum);
	win_goto(win);
	return win;
    }
    win = NULL;

    /* use a non-console window */
    if (curbuf == this->buf)
	for (win = firstwin; win != NULL; win = win->w_next)
	    if (win->w_buffer != this->buf)
	    {
		win_goto(win);
		break;
	    }

    if (win == NULL)
	win = curwin;

    if (buf == NULL)
    {
	/* get the first existing full path name in GDB source directories
	 * matching this name */
	if ((fpn = (char_u *)get_fullpath((char *)fname, (char *)this->directories,
			(char *)source_cur, (char *)source_list, obs)) == NULL)
	{
	    return NULL;
	}

	(void)do_ecmd(0, fpn, NULL, NULL, lnum,
		      (P_HID(curbuf) ? ECMD_HIDE : 0) + ECMD_OLDBUF);
    }
    else if (buf != curbuf)
	set_curbuf(buf, DOBUF_GOTO);

    /* asm buffers are not listed */
    if (buf != NULL && buf == curbuf)
	for (i = 0; i < this->pool.max; i++)
	    if (this->pool.buf[i] == buf)
	    {
		set_option_value((char_u *)"bl", 0L, NULL, OPT_LOCAL);
		break;
	    }

    if (win_valid(win))
    {
	gdb_set_cursor(win, lnum);
	prevwin = win;
	return win;
    }

    return NULL;
}

/* Set cursor to lnum with at least SCROLLOFF lines around it */
    void
gdb_set_cursor(win, lnum)
    win_T *win;
    linenr_T lnum;
{
    win_T *oldwin = curwin;
    long s_so = p_so;

    if (win != NULL)
    {
	curwin = win;
	curbuf = curwin->w_buffer;

	curwin->w_cursor.lnum = lnum;
	curwin->w_cursor.col = 0;
	check_cursor();

	if (p_so < SCROLLOFF )
	    p_so = SCROLLOFF;
	update_topline();

	win->w_redr_status = TRUE;

	p_so = s_so;
	curwin = oldwin;
	curbuf = curwin->w_buffer;
    }
}

/* Display a cmd line busy msg */
    void
gdb_msg_busy(str)
    char_u *str;
{
    static char *prop[] = { "/", "-", "\\", "|" };
    static char busy[IOSIZE];
    static int cnt;

    /* set busy string */
    if (str != NULL)
    {
	STRNCPY(busy, str, IOSIZE - 1);
	busy[IOSIZE - 1] = NUL;
    }
    else
    {
	msg_scroll = FALSE;
	msg_start();
	msg_outtrans((char_u *)busy);
	msg_puts((char_u *)" [");
	msg_puts((char_u *)prop[(++cnt % 4)]);
	msg_puts((char_u *)"]");
	msg_clr_eos();
	out_flush();
    }
}

/* Set status and and mark stl dirty */
    void
gdb_status(this, status, obs)
    gdb_T *this;
    char_u *status;	/* gdb status */
    struct obstack *obs;
{
    win_T *win;

    if (obs) {}	    /* keep comiler happy */

    xfree(this->status);
    this->status = (char_u *)clewn_strsave((char *)status);
    if ((win = gdb_btowin(this->buf)) != NULL)
	win->w_redr_status = TRUE;
}

/* Update screen when buf is displayed */
/* needed only from low level functions, not elsewhere */
    void
gdb_redraw(buf)
    buf_T *buf;
{
    int s_r = RedrawingDisabled;
    int s_lz = p_lz;
    win_T *win;

    if (buf == NULL)
	return;

    for (win = firstwin; win != NULL; win = win->w_next)
	if (win->w_buffer == buf)
	{
	    RedrawingDisabled = 0;
	    p_lz = FALSE;
	    update_screen(0);
	    RedrawingDisabled = s_r;
	    p_lz = s_lz;

	    /* reset cursor */
	    cursor_on();
	    setcursor();
	    out_flush();
	    break;
	}
}

/* Do the OOB_COMPLETE part of an oob cmd and send the next one */
    void
gdb_oob_send(this, obs)
    gdb_T *this;
    struct obstack *obs;
{
    int keep    = FALSE;    /* when TRUE, do not switch to next oob function */
    char *res   = NULL;
    int *pi     = &(this->oob.idx);
    int s_a     = (this->state & GS_ALLOWED);

    /* prevent recursive calls to parse_output() since breakpoint
     * or frame highlighting may cause Vim to query the user when
     * changes have been made in the previous buffer */
    this->state &= ~GS_ALLOWED;

    if (this->oobfunc == NULL)
	return;

    if (*pi == -1)
    {
	this->oob.state &= ~OS_INTR;
	if (this->oob.state & OS_QUIT)
	    goto quit;
    }

    if (*pi >= 0 && (this->oobfunc)[*pi].oob != NULL) /* assert != NULL */
    {
	if ((this->oobfunc)[*pi].oob(this, OOB_COMPLETE, NULL, obs) != NULL)
	    keep = TRUE;

	if (this->oob.state & OS_QUIT)
	    goto quit;
    }

    if (! keep)
	++(*pi);

    while ((this->oobfunc)[*pi].oob != NULL && !(this->oob.state & OS_INTR))
    {
	if ((res = (this->oobfunc)[*pi].oob(this, OOB_CMD, NULL, obs)) != NULL)
	{
	    this->oob.cnt = 0;

	    /* send the command to GDB */
	    write(this->fd, res, strlen(res));

	    this->state &= ~GS_ALLOWED;
	    if (s_a)
		this->state |= GS_ALLOWED;
	    return;
	}

	++(*pi);
    }

    *pi = -1;
quit:
    this->oob.state &= ~OS_CMD;
    this->oob.state &= ~OS_QUIT;

    this->state &= ~GS_ALLOWED;
    if (s_a)
	this->state |= GS_ALLOWED;
}

/* Receive out of band response to idx cmd */
    void
gdb_oob_receive(this, chunk, obs)
    gdb_T *this;
    char_u *chunk;	/* response (possibly incomplete) */
    struct obstack *obs;
{
    char_u *res = NULL;
    int s_a = (this->state & GS_ALLOWED);

    /* prevent recursive calls to parse_output() since breakpoint
     * or frame highlighting may cause Vim to query the user when
     * changes have been made in the previous buffer */
    this->state &= ~GS_ALLOWED;

    if (this->oobfunc == NULL)
	return;

    if(IS_OOBACTIVE(this))
    {
	/* silently discard when interrupted */
	if (!(this->oob.state & OS_INTR) && chunk != NULL)
	{
	    if (this->parser != PS_PREPROMPT && this->parser != PS_PROMPT
		&& (this->oobfunc)[this->oob.idx].oob != NULL) /* assert != NULL */
	    {
		this->oob.cnt++;
		(void)(this->oobfunc)[this->oob.idx].oob(this, OOB_COLLECT, chunk, obs);

		this->state &= ~GS_ALLOWED;
		if (s_a)
		    this->state |= GS_ALLOWED;
		return;
	    }
	}

	/* keep the last prompt */
	if (this->parser == PS_PREPROMPT)
	{
	    gdb_cat(&res, this->line);
	    gdb_cat(&res, chunk);
	    xfree(this->line);
	    this->line = res;
	}
    }

    this->state &= ~GS_ALLOWED;
    if (s_a)
	this->state |= GS_ALLOWED;
}

/*
 * Fill up buff with a NUL terminated string of max size - 1 bytes from gdb.
 * Return bytes read count, -1 if error or zero for nothing to read.
 */
    int
gdb_read(this, buff, size, wtime)
    gdb_T *this;
    char_u *buff;	/* where to write */
    int size;		/* buff size */
    int wtime;		/* msecs time out, -1 wait forever */
{
    int len;
    int rc;
# ifndef HAVE_SELECT
    struct pollfd fds;

    fds.fd = this->fd;
    fds.events = POLLIN;
# else
    struct timeval tv;
    struct timeval start_tv;
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(this->fd, &rfds);

#  ifdef HAVE_GETTIMEOFDAY
    if (wtime >= 0)
	gettimeofday(&start_tv, NULL);
#  endif
# endif

    if (size <= 0 || buff == NULL || !(this->state & GS_UP))
	return -1;

    /* make sure there is some data to read */
    while (1)
    {
	if (this->state & GS_SIGCHLD)
	    goto close;

# ifndef HAVE_SELECT
	if ((rc = poll(&fds, 1, wtime)) > 0)
# else
	if (wtime >= 0)
	{
	    tv.tv_sec = wtime / 1000;
	    tv.tv_usec = (wtime % 1000) * (1000000/1000);
	}

	if ((rc = select(this->fd + 1, &rfds, NULL, NULL, (wtime >= 0) ? &tv : NULL)) > 0)
# endif
	    break;

	if (rc == -1 && errno == EINTR)
	{
	    if (wtime >= 0)
	    {
		/* compute remaining wait time */
# if ! defined(HAVE_SELECT) || ! defined(HAVE_GETTIMEOFDAY)
		/* guess: interrupted halfway, gdb processing 10 msecs */
		wtime = wtime / 2 - 10L;
# else
		gettimeofday(&tv, NULL);
		wtime -= (tv.tv_sec - start_tv.tv_sec) * 1000L
				+ (tv.tv_usec - start_tv.tv_usec) / 1000L;
# endif
		if (wtime < 0)
		    return 0;
	    }
	}
	else if (rc == 0)
	    return 0;
	else
	    goto close;
    }

    /* read the data */
    if ((len = read(this->fd, (char *)buff, size - 1)) < 0)
	goto close;

    buff[len] = NUL;
    return len;
close:
    gdb_close(this);
    return -1;
}

/* Free a bpinfo_T list and set address referenced by plist to NULL */
    void
gdb_free_bplist (plist)
    bpinfo_T ** plist;
{
    bpinfo_T *p, *next;

    if (plist == NULL)
	return;

    for (p = *plist; p != NULL; p = next)
    {
	gdb_undefine_sign(p->typenr);
	next = p->next;
	xfree(p);
    }

    *plist = NULL;
}

/* Remove str from input history */
    static void
histdel(str)
    char_u *str;	/* string to remove */
{
    char_u *pat;
    char_u *res;

    if (str != NULL && (res = vim_strsave_escaped(str, (char_u *)"\\")) != NULL)
    {
	/* nomagic: only the backslash has a special meaning */
	pat = NULL;
	gdb_cat(&pat, (char_u *)"\\V\\^");
	gdb_cat(&pat, res);
	gdb_cat(&pat, (char_u *)"\\$");

	(void)del_history_entry(HIST_INPUT, pat);
	xfree(pat);

	xfree(res);
    }
}

/* Get the GDB command type */
    void
gdb_cmd_type(this, cmd)
    gdb_T *this;
    char_u *cmd;
{
    regmatch_T regmatch;
    token_T *tok;

    regmatch.rm_ic = FALSE;	/* always match case */
    this->cmd_type = CMD_ANY;
    for (tok = tokens; tok->keyword != NULL; tok++)
	if ((regmatch.regprog = tok->regprog) != NULL
		&& vim_regexec(&regmatch, cmd, (colnr_T)0))
	{
	    this->cmd_type = tok->type;
	    break;
	}
}

/*
 * Append src to string pointed to by pdest or copy src to a new allocated
 * string when *pdest is NULL.
 * *pdest is reallocated to make room for src.
 * Append an empty string when src is NULL.
 */
    void
gdb_cat(pdest, src)
    char_u **pdest;	/* string address to append to */
    char_u *src;	/* string to append */
{
    int ldest = (*pdest != NULL ? STRLEN(*pdest) : 0);
    int lsrc  = (src != NULL ? STRLEN(src) : 0);
    char_u *res;

    if (lsrc != 0 || *pdest == NULL)
    {
	res = (char_u *)xmalloc(ldest + lsrc + 1);

	if (ldest == 0)
	{
	    if (lsrc != 0)
		STRCPY(res, src);
	    else
		STRCPY(res, "");
	}
	else
	{
	    STRCPY(res, *pdest);
	    STRCAT(res, src);	/* assert src != NULL */
	}

	xfree(*pdest);
	*pdest = res;
    }
}

/*
 * Return an allocated string that is the sub-match indexed by subid ([0-9])
 * using compiled pattern id.
 * Return NULL if str does not match (or no such sub-match in pattern).
 */
    char_u *
gdb_regexec(str, id, subid, obs)
    char_u *str;	/* string to match against */
    int id;		/* pattern id */
    int subid;		/* sub-match index */
    struct obstack *obs;/* obstack to use for allocating memory */
{
    pattern_T *pat;
    regmatch_T regmatch;

    if (str == NULL || *str == NUL || subid < 0 || subid > 9)
	return NULL;

    for (pat = patterns; pat->str != NULL; pat++)
	if (pat->id == id)
	{
	    regmatch.rm_ic = FALSE;	/* always match case */
	    if ((regmatch.regprog = pat->regprog) != NULL
		    && vim_regexec(&regmatch, str, (colnr_T)0)
		    && regmatch.startp[subid] != NULL)
	    {
		if (obs != NULL)
		    return (char_u *)obstack_copy0(obs, regmatch.startp[subid],
			    (int)(regmatch.endp[subid] - regmatch.startp[subid]));
		else
		    return (char_u *)clewn_strnsave((char *)regmatch.startp[subid],
			    (int)(regmatch.endp[subid] - regmatch.startp[subid]));
	    }
	    break;
	}
    return NULL;
}

/* Return an integer as a string */
    char_u *
gdb_itoa(i)
    int i;		/* integer to stringify */
{
    static char buf[NUMBUFLEN];

    sprintf(buf, "%ld", (long)i);
    return (char_u *)buf;
}
#endif /* FEAT_GDB */
