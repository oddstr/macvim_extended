/* vi:set ts=8 sts=4 sw=4:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 * 			CodeCheck extension by Birgi Tamersoy
 *				birgitamersoy@gmail.com
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * code_check.c: On-the-fly syntax checking tool.
 */

#include <pthread.h>
#include <semaphore.h>
#include "vim.h"

#define CC_VERSION  0.1

enum cc_ret_vals {
    CC_FAIL = 0,
    CC_SUCCESS,
    CC_BUFEXISTS,
    CC_NOSUCHBUF,
};

enum cc_ew_types {
    CC_NOEW = 0,
    CC_WARNING,
    CC_ERROR
};

enum cc_search_mode {
    CC_FOR_ADD = 0,
    CC_FOR_REM,
    CC_FOR_FIND
};

enum cc_print_direc {
    CC_FWD = 0,
    CC_REW
};

enum cc_copy_type {
    CC_STANDALONE = 0,
    CC_MASTER,
    CC_PROJECT		/* Makefile exists */
};

/*
 * Multiple buffers can be syntax checked simultaneously and each buffer 
 * has its own list of errors and warnings. Consequently we have two list
 * structures. One holds information about processed buffers and the other
 * one holds information about buffer-specific errors & warnings.
 *
 * Both lists will be accessed frequently, so they will be sorted:
 *  buffer list will be sorted with respect to full file names, and
 *  error/warning list will be sorted with respect to line numbers.
 */

#define	MAX_EW_TEXT 200

/* compile command length */
#define MAX_CMD_LENGTH 600

#define MAX_PATH_LENGTH 300

typedef struct cc_ewline_S cc_ewline_T;
struct cc_ewline_S {
    cc_ewline_T	*prev;
    cc_ewline_T	*next;
    int		ew_type;
    char_u	ew_text[MAX_EW_TEXT];
    linenr_T	ew_lnum;
};

typedef struct cc_bufline_S cc_bufline_T;
struct cc_bufline_S {
    cc_bufline_T    *prev;
    cc_bufline_T    *next;
    char_u	    *buf_name;
    cc_ewline_T	    *buf_ewlist_head;
    pthread_mutex_t buf_mutex;
    char_u	    buf_compile_cmd[MAX_CMD_LENGTH];
};

typedef struct cc_info_S cc_info_T;
struct cc_info_S {
    int		    cc_bufcount;
    cc_bufline_T    *cc_list_head;
    cc_bufline_T    *cc_list_curr;
};

/* global list of buffers. */
static cc_info_T cc_list;
#define MAX_BUFLINES 100
static cc_bufline_T *cc_bufline_ptrs[MAX_BUFLINES];

/* supported languages, the format should be ".<ext1>" ...  */
/* each extension should padded with ' ' characters, st. total is 5 chars */ 
static char_u *cc_sup_exts = (char_u *) ".c   "
					".cpp ";

/* worker thread. */
static pthread_t cc_slave;

/* a queue for the pending jobs that the worker thread should handle.
 * basically a list of buffer compile requests. */
enum cc_pjob_types {
    CC_COMPILE = 0
};

typedef struct cc_pjob_S cc_pjob_T;
struct cc_pjob_S {
    int		cc_pjob_type;
    buf_T	*cc_pjob_buf;
};

#define MAX_PENDING_JOBS 10
static cc_pjob_T *cc_pjobs[MAX_PENDING_JOBS];
static int pindex = 0;
static int cindex = 0;

static sem_t full;
static sem_t empty;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* functions */
static void cc_free_ewlist(cc_ewline_T *ewlist_head);
static cc_bufline_T *cc_locate_buf(buf_T *buf);
static cc_bufline_T *cc_locate_buf_bin(buf_T *buf, int *buf_idx, 
	int *put_before, int mode);
static void cc_free_buflist(cc_info_T *cc_list_a, 
	cc_bufline_T *cc_bufline_ptrs_a[MAX_BUFLINES]);
static int cc_start_slave_thread(void);
static void *cc_slave_sroutine(void *args);
static int cc_pjobs_produce(cc_pjob_T *tmp_pjob);
static cc_pjob_T *cc_pjobs_consume(void);
static int cc_pjobs_buf_exists(cc_pjob_T *tmp_pjob);
static int cc_create_tmp_copy(buf_T *buf, char_u *tmp_copy_ffname, 
	int copy_type);
static int cc_compile_tmp_copy(cc_bufline_T *bufline, 
	char_u *tmp_copy_ffname, int copy_type);
static cc_ewline_T *cc_create_ewlist(char_u *tmp_out_ffname);
void cc_sigalrm_handler(int signum);
void cc_update_screen(void);
static void cc_free(void);
static cc_bufline_T* cc_add_buf(buf_T *buf);
static int cc_set_tmp_copy_ffname(buf_T *buf, char_u *tmp_copy_ffname);

static int cc_started = FALSE;
static int old_p_ut = 0;

/*
 * TODO:
 *
 * 2. error/warning format should be added so that new languages are 
 *	    easily added to the code_check.c.
 */

/*
 * Initializes the related structures.
 */
    int
cc_init(void) {
    int	    i;
    int	    retval;

    cc_list.cc_bufcount = 0;
    cc_list.cc_list_head = NULL;
    cc_list.cc_list_curr = NULL;

   retval = sem_init(&full, 0, 0); 
   if (retval)
       return CC_FAIL;

   retval = sem_init(&empty, 0, MAX_PENDING_JOBS); 
   if (retval)
       return CC_FAIL;

    cc_start_slave_thread();

    for (i = 0; i < MAX_BUFLINES; ++i)
	cc_bufline_ptrs[i] = NULL;

    for (i = 0; i < MAX_PENDING_JOBS; ++i)
	cc_pjobs[i] = NULL;

    old_p_ut = p_ut;
    p_ut = 1000;

    cc_started = TRUE;

    return CC_SUCCESS;
}

/*
 * Returns the current number of buffers in the watchlist.
 */
    int
cc_get_bufcount(void) {
    return cc_list.cc_bufcount;
}

/*
 * Returns TRUE if CodeCheck is already started.
 */
    int
cc_get_is_started(void) {
    return cc_started;
}

/*
 * CodeCheck creates a working thread which would run in the background.
 * This thread is responsible of compiling the specified buffers, parsing 
 * the outputs and forming the corresponding error/warning lists.
 */
    static int
cc_start_slave_thread(void) {
    pthread_attr_t  *attr	= NULL;
    int		    retval;

    attr = (pthread_attr_t *) calloc(1, sizeof(pthread_attr_t));

    retval = pthread_attr_init(attr);
    if (retval)
	goto sslave_fail;

    retval = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED);
    if (retval)
	goto sslave_fail;

    retval = pthread_create(&cc_slave, attr, cc_slave_sroutine, NULL);
    if (retval)
	goto sslave_fail;

    return CC_SUCCESS;

sslave_fail:
    free(attr);
    return CC_FAIL;
}

/*
 * Function updates the error/warning line numbers after new lines are inserted
 * or some existing lines are removed. This is just to move the highlighted
 * parts before an actual compile.
 *
 * TODO: This function is mainly copied from color_me.c (the preliminary 
 * project). So, double check if everything is OK or not!!!
 */
    int
cc_update_ew_lnums(buf_T *buf, int lnum, int col, long xtra) {
    if (!cc_started)
	return CC_FAIL;

    cc_bufline_T    *tmp_bufline;
    cc_ewline_T	    *tmp_ewline;
    char_u	    *line	    = NULL;
    int		    dummy;
    int		    i;
    int		    skip	    = FALSE;

    tmp_bufline = cc_locate_buf_bin(buf, &dummy, &dummy, CC_FOR_FIND);
    if (tmp_bufline == NULL)
	return CC_FAIL;

    pthread_mutex_lock(&(tmp_bufline->buf_mutex));

    for (tmp_ewline = tmp_bufline->buf_ewlist_head;
	    tmp_ewline != NULL; tmp_ewline = tmp_ewline->next) {

	    skip = FALSE;
	    if (tmp_ewline->ew_lnum < lnum)
		    continue;
	    if (tmp_ewline->ew_lnum == lnum) {
		    /* is this the beginning of the line? */
		    line = ml_get_buf(buf, lnum, FALSE);
		    for (i = col - 1; i > 0 && 
				    (line[i] == '\t' || line[i] == ' '); --i)
			    ;
		    if (i > 0)
			    continue;
	    }
	    tmp_ewline->ew_lnum += xtra;
    }

    pthread_mutex_unlock(&(tmp_bufline->buf_mutex));
    return CC_SUCCESS;
}

/* 
 * Worker thread function, which continuously checks for unserved requests.
 */
    static void *
cc_slave_sroutine(void *args) {
    cc_pjob_T	    *tmp_pjob;
    cc_bufline_T    *tmp_bufline;
    int		    retval;
    int		    dummy;
    char_u	    tmp_copy_ffname[MAX_PATH_LENGTH];
    char_u	    tmp_out_ffname[MAX_CMD_LENGTH];
    cc_ewline_T	    *ew_head = NULL;

    while (TRUE) {
	/* consume a pending job */
	tmp_pjob = cc_pjobs_consume();
	if (tmp_pjob == NULL)
	    continue;

	/* when gui mode is used, an other update screen is required. */
	/*				    i don't know why :)!!!. */
#ifdef FEAT_GUI
	if (gui.in_use)
	    cc_update_screen();
#endif

	/* find the buffer line */

	/* create the temporary copy */
	retval = cc_create_tmp_copy(tmp_pjob->cc_pjob_buf, 
		tmp_copy_ffname, CC_STANDALONE);
	if (retval == CC_FAIL) 
	    continue;

	tmp_bufline = cc_locate_buf_bin(tmp_pjob->cc_pjob_buf, &dummy, 
		&dummy, CC_FOR_FIND);
	if (tmp_bufline == NULL)
	    continue;

	/* compile the temporary copy */
	retval = cc_compile_tmp_copy(tmp_bufline, tmp_copy_ffname, 
		CC_STANDALONE);
	if (retval == CC_FAIL) 
	    continue;

	/* create the error list from the temporary copy */
	sprintf((char *)tmp_out_ffname, "%s.out", (char *)tmp_copy_ffname);
	ew_head = cc_create_ewlist(tmp_out_ffname);

	/* having ew_head == NULL is not an error, since all
	 * the errors and the warnings may be cleaned. */

	/* free the previous list & set the new list */
	pthread_mutex_lock(&(tmp_bufline->buf_mutex));

	cc_free_ewlist(tmp_bufline->buf_ewlist_head);
	tmp_bufline->buf_ewlist_head = ew_head;

	pthread_mutex_unlock(&(tmp_bufline->buf_mutex));

	/* update the screen */
	cc_update_screen();

	/* that would be it for this buffer */
    }
}

/* 
 * Function is used to update the screen properly.
 */
    void
cc_update_screen(void) {
#ifdef FEAT_GUI
    if (!gui.in_use) {
#endif
	/* in console mode updating the screen from the worker thread
	 * does not cause any problems. */
	update_topline();
	validate_cursor();
	update_screen(SOME_VALID);
	setcursor();
	cursor_on();
	out_flush();
#ifdef FEAT_GUI
    } else {
	/* updating the screen in gui mode is troublesome. */
	char_u	bytes[3];

	bytes[0] = CSI;
	bytes[1] = KS_EXTRA;
	bytes[2] = KE_REDRAW;

	add_to_input_buf(bytes, 3);
    }
#endif
}

/*
 * Function returns the correct error/warning type for a specific lnum.
 * Returns CC_NOEW if the lnum does not have an error or a warning.
 */
    int
cc_get_ew_type(buf_T *buf, linenr_T lnum) {
    if (!cc_started)
	return CC_FAIL;

    cc_bufline_T    *tmp_bufline;
    cc_ewline_T	    *tmp_ewline;
    int		    dummy;
    int		    ew_type	    = CC_NOEW;

    tmp_bufline = cc_locate_buf_bin(buf, &dummy, &dummy, CC_FOR_FIND);
    if (tmp_bufline == NULL)
	return CC_NOEW;

    pthread_mutex_lock(&(tmp_bufline->buf_mutex));
    tmp_ewline = tmp_bufline->buf_ewlist_head;
    while (tmp_ewline != NULL) {
	if (tmp_ewline->ew_lnum != lnum) {
	    tmp_ewline = tmp_ewline->next;
	    continue;
	}

	if (ew_type < tmp_ewline->ew_type)
	    ew_type = tmp_ewline->ew_type;

	tmp_ewline = tmp_ewline->next;
    }
    pthread_mutex_unlock(&(tmp_bufline->buf_mutex));
    return ew_type;
}

/*
 * Function creates a quickfix error list from the compiler output.
 * TODO: Right now works only for .c files and the gcc compiler.
 *	Should be automated to work with multiple languages and multiple
 *	compilers.
 */
    static cc_ewline_T *
cc_create_ewlist(char_u *tmp_out_ffname) {
    FILE	    *err_file	= NULL;
    char_u	    *buf	= NULL; 
    size_t	    len		= 0;
    size_t	    read;
    cc_ewline_T	    *ew_head	= NULL;
    cc_ewline_T	    *ew_curr	= NULL;
    char_u	    *token1	= NULL;
    char_u	    *token2	= NULL;
    char_u	    *token3	= NULL;
    char_u	    *token4	= NULL;
    cc_ewline_T	    *tmp_ewline = NULL;


    err_file = fopen((char *)tmp_out_ffname, "r");
    if (err_file == NULL)
	return NULL;

    while ((read = getline((char **) &buf, &len, err_file)) != -1) {
	token1 = (char_u *)strtok((char *) buf, ":");
	token2 = (char_u *)strtok(NULL, ":");
	token3 = (char_u *)strtok(NULL, ":");
	token4 = (char_u *)strtok(NULL, ":");

	/* TODO: be sure it is gXX type of output. */

	if (!token4)
	    continue;

	tmp_ewline = (cc_ewline_T *) calloc(1, sizeof(cc_ewline_T));
	STRCPY(tmp_ewline->ew_text, token4);
	/* remove the last '\n' from the error/warning message */
	tmp_ewline->ew_text[STRLEN(tmp_ewline->ew_text) - 1] = '\0';
	tmp_ewline->ew_lnum = (linenr_T) atoi((char *) token2);
	tmp_ewline->ew_type = token3[1] == 'w' ? CC_WARNING : CC_ERROR;

	if (!ew_head) {
	    ew_head = tmp_ewline;
	    ew_curr = tmp_ewline;
	} else {
	    ew_curr->next = tmp_ewline;
	    tmp_ewline->prev = ew_curr;
	    ew_curr = ew_curr->next;
	}
    }

    /* buf should be freed, because it is allocated in getline */
    if (buf)
	free(buf);

    return ew_head;
}

/*
 * Function compiles the temporary copy saving the output in a
 * temporary file.
 */
    static int
cc_compile_tmp_copy(cc_bufline_T *bufline, char_u *tmp_copy_ffname, 
	int copy_type) {
    char_u	cmd[MAX_CMD_LENGTH];
    int		retval;

    if (bufline->buf_compile_cmd[0] == NUL)
	return CC_FAIL;

    /* clear the arrays. */
    memset(cmd, 0, MAX_CMD_LENGTH);

    /* direct STDOUT & STDERR to the same file, so that the output of
     * this will not interfer with the vim terminal */
    sprintf((char *)cmd, "%s > %s.out 2>&1", 
	    (char *)bufline->buf_compile_cmd, (char *)tmp_copy_ffname);
#if 1    
    retval = system((char *)cmd);
    if (retval < 0)
	return CC_FAIL;
#endif

    return CC_SUCCESS;
}

/*
 * Function creates a temporary copy of the buffer. copy_type determines
 * the type of the copy process.
 */
    static int
cc_create_tmp_copy(buf_T *buf, char_u *tmp_copy_ffname, int copy_type) {
    int		retval;
    char_u	cmd[MAX_CMD_LENGTH];

    switch (copy_type) {
	case CC_STANDALONE:
	    cc_set_tmp_copy_ffname(buf, tmp_copy_ffname);
	    sprintf((char *)cmd, "touch %s", (char *)tmp_copy_ffname);
    
	    retval = system((char *)cmd);
	    if (retval < 0)
		return CC_FAIL;
#if 1
	    retval = buf_write(buf, tmp_copy_ffname, NULL,
		    (linenr_T) 1, buf->b_ml.ml_line_count, NULL, 
		    FALSE, FALSE, FALSE, TRUE);

	    if (retval == FAIL)
		return CC_FAIL;
	    else
#endif
		return CC_SUCCESS;

	/* these are not implemented yet. */
	case CC_MASTER:
	case CC_PROJECT:
	default:
	    return CC_FAIL;
    }
}

/*
 * Outside accessible compile request for a specific buffer.
 */
    int
cc_request_compile(buf_T *buf) {
    if (!cc_started)
	return CC_FAIL;

    cc_pjob_T	*tmp_pjob;

    tmp_pjob = (cc_pjob_T *) calloc(1, sizeof(cc_pjob_T));
    if (tmp_pjob == NULL)
	return CC_FAIL;

    tmp_pjob->cc_pjob_buf = buf;
    tmp_pjob->cc_pjob_type = CC_COMPILE;

    return cc_pjobs_produce(tmp_pjob);
}

/*
 * Function required to populate the pending jobs array.
 * TODO: FAILURE IN pthread_mutex_unlock is SERIOUS!!!
 */
    static int
cc_pjobs_produce(cc_pjob_T *tmp_pjob) {
    int	    retval;

    sem_wait(&empty);

    retval = pthread_mutex_lock(&mutex);
    if (retval)
	return CC_FAIL;

    /* check if this buffer already has a pending job request. */
    /* if so directly return. */
    if (cc_pjobs_buf_exists(tmp_pjob)) {
	sem_post(&empty);
	retval = pthread_mutex_unlock(&mutex);
	if (retval)
	    return CC_FAIL;
	return CC_BUFEXISTS;
    }

    cc_pjobs[pindex % MAX_PENDING_JOBS] = tmp_pjob;
    ++pindex;

    retval = pthread_mutex_unlock(&mutex);
    if (retval)
	return CC_FAIL;

    retval = sem_post(&full);
    if (retval)
	return CC_FAIL;

    return CC_SUCCESS;
}

/*
 * Returns TRUE if there is another pending job for this process.
 */
    static int
cc_pjobs_buf_exists(cc_pjob_T *tmp_pjob) {
    int	    i;

    for (i = 0; i < MAX_PENDING_JOBS; ++i) {
	if (cc_pjobs[i] == NULL || 
		strcmp((char *) cc_pjobs[i]->cc_pjob_buf->b_ffname,
		    (char *) tmp_pjob->cc_pjob_buf->b_ffname))
	    continue;
	else
	    return TRUE;
    }
    return FALSE;
}

/*
 * Removes the "top" element.
 */
    static cc_pjob_T *
cc_pjobs_consume(void) {
    int		retval;
    cc_pjob_T	*tmp_pjob   = NULL;

    tmp_pjob = (cc_pjob_T *) calloc(1, sizeof(cc_pjob_T));
    if (tmp_pjob == NULL)
	return NULL;

    sem_wait(&full);

    retval = pthread_mutex_lock(&mutex);
    if (retval)
	goto con_fail;

    tmp_pjob->cc_pjob_type = cc_pjobs[cindex % MAX_PENDING_JOBS]->cc_pjob_type;
    tmp_pjob->cc_pjob_buf = cc_pjobs[cindex % MAX_PENDING_JOBS]->cc_pjob_buf;

    /* free the old job!!! */
    free(cc_pjobs[cindex % MAX_PENDING_JOBS]);
    cc_pjobs[cindex % MAX_PENDING_JOBS] = NULL;

    cindex++;
    
    retval = pthread_mutex_unlock(&mutex);
    if (retval)
	goto con_fail;

    retval = sem_post(&empty);
    if (retval)
	goto con_fail;

    return tmp_pjob;

con_fail:
    free(tmp_pjob);
    return NULL;
}

/*
 * Checks if the specified buffer can be syntax checked or not.
 */
    int
cc_is_buf_ok(buf_T *buf) {
    if (!cc_started)
	return CC_FAIL;

    char_u  *ffname	= NULL;
    char_u  *p		= NULL;
    char_u  *r		= NULL;
    
    if (buf == NULL || ((ffname = buf->b_ffname) == NULL))
	return CC_FAIL;

    /* find the file extension */
    p = (char_u *) strrchr((char *) ffname, '.');

    if (p == NULL)
	return CC_FAIL;

    /* check if this extension is supported or not */
    r = (char_u *) strstr((char *) cc_sup_exts, (char *) p);

    if (r == NULL)
	return CC_FAIL;
    else 
        return CC_SUCCESS;
}

/*
 * Returns TRUE if the specified buffer is in the watchlist.
 * TODO: THIS CAN RETURN THE FOUND BUFFER TO SAVE SOME TIME.
 *
 */
    int
cc_is_buf_watched(buf_T *buf) {
    if (!cc_started)
	return CC_FAIL;

    cc_bufline_T    *tmp_bufline;
    int		    dummy;

    tmp_bufline = cc_locate_buf_bin(buf, &dummy, &dummy, CC_FOR_FIND);
    if (tmp_bufline == NULL)
	return FALSE;
    else 
	return TRUE;
}

/*
 * Finds and sets a file name for the temporary copy of the buffer.
 */
    static int
cc_set_tmp_copy_ffname(buf_T *buf, char_u *tmp_copy_ffname) {
    char_u	*buf_sfname;
    char_u	tmp_buf[MAX_PATH_LENGTH];
    char_u	*p;

    /* clear the array. */
    memset(tmp_buf, 0, MAX_PATH_LENGTH);

    /* find the tmp_copy_ffname. */
    /* uses gettail(buf->b_sfname) because sometimes b_sfname is 
     * actually b_ffname. */
    buf_sfname = buf->b_sfname ? gettail(buf->b_sfname) 
				: gettail(buf->b_ffname);
    if (buf_sfname == NULL || !strcmp((char *) buf_sfname, ""))
	return CC_FAIL;

    /* finds the folder path of the buffer. */
    p = (char_u *)strstr((char *)buf->b_ffname, (char *)buf_sfname);
    if (p == NULL)
	return CC_FAIL;
    STRNCPY(tmp_buf, buf->b_ffname, (p - buf->b_ffname));

    /* rather than creating the copy in /tmp/, create it in the directory
     * of the buffer. this solves a few issues without a lot of effort. */
    sprintf((char *) tmp_copy_ffname, "%s.cc_%s", 
	    (char *) tmp_buf, (char *) buf_sfname);
    return CC_SUCCESS;
}

/*
 * Adds the specified buffer and sets the compile command.
 */
    int
cc_addbuf_setcmd(buf_T *buf, char_u *cmd) {
    cc_bufline_T    *tmp_bufline    = NULL;
    char_u	    tmp_compile_cmd[MAX_CMD_LENGTH];
    char_u	    *p		    = NULL;
    char_u	    tmp_copy_ffname[MAX_PATH_LENGTH];
    size_t	    bname_len;

    /* clear the arrays. */
    memset(tmp_compile_cmd, 0, MAX_CMD_LENGTH);
    memset(tmp_copy_ffname, 0, MAX_PATH_LENGTH);

    tmp_bufline = cc_add_buf(buf);
    if (tmp_bufline == NULL)
	return CC_FAIL;

    cc_set_tmp_copy_ffname(buf, tmp_copy_ffname);

    /* at this point we should update the compile command, so
     * that it works for the temporary copy, rather than the 
     * original copy. 
     * *** assumes that the user entered compile command has the 
     * full file name of the current buffer. */
    p = (char_u *)strstr((char *)cmd, 
	    (char *)tmp_bufline->buf_name);
    if (p == NULL)
	return CC_FAIL;
    bname_len = STRLEN(tmp_bufline->buf_name);

    STRNCPY(tmp_compile_cmd, cmd, 
	    (p - cmd));
    STRCAT(tmp_compile_cmd, tmp_copy_ffname);
    STRCAT(tmp_compile_cmd, p + bname_len);

    STRCPY(tmp_bufline->buf_compile_cmd, tmp_compile_cmd);

    /* TODO: check if tmp_bufline->buf_compile_cmd is valid. */
    return CC_SUCCESS;
}

/*
 * Adds the specified buffer to the watch list.
 *
 * modified a little: returns a pointer to the inserted cc_bufline_T 
 * object, to make things a little faster.
 *
 */
    static cc_bufline_T*
cc_add_buf(buf_T *buf) {
    if (!cc_started)
	return NULL;

    cc_bufline_T    *tmp_bufline    = NULL;
    cc_bufline_T    *tmp_insafter   = NULL;
    int		    buf_idx;
    int		    put_before;
    int		    i;
    int		    retval;

    /* check if this buffer is in the watch list, if so don't add */
    tmp_bufline = cc_locate_buf_bin(buf, &buf_idx, &put_before, CC_FOR_FIND);
    if (tmp_bufline != NULL)
	return NULL;

    if (cc_list.cc_bufcount == MAX_BUFLINES)
	return NULL;
    
    tmp_bufline = (cc_bufline_T *) calloc(1, sizeof(cc_bufline_T));

    if (tmp_bufline == NULL)
	return NULL;

    tmp_bufline->buf_name = buf->b_ffname;
    memset(tmp_bufline->buf_compile_cmd, 0, MAX_CMD_LENGTH);
    pthread_mutex_init(&(tmp_bufline->buf_mutex), NULL);

    if (cc_list.cc_bufcount == 0) {
	/* this is the first buffer */
	cc_list.cc_list_head = tmp_bufline;
	tmp_bufline->prev = NULL;
	tmp_bufline->next = NULL;
	cc_bufline_ptrs[0] = tmp_bufline;
    } else {
	tmp_insafter = cc_locate_buf_bin(buf, &buf_idx, &put_before, CC_FOR_ADD);
	
	if (tmp_insafter == NULL) {
	    free(tmp_bufline);
	    return NULL;
	}

	if (put_before) {
	    /* move elements in cc_bufline_ptrs */
	    for (i = cc_list.cc_bufcount; i > buf_idx; --i)
		cc_bufline_ptrs[i] = cc_bufline_ptrs[i - 1];

	    cc_bufline_ptrs[buf_idx] = tmp_bufline;

	    tmp_bufline->prev = tmp_insafter->prev;
	    if (tmp_insafter->prev != NULL)
		tmp_insafter->prev->next = tmp_bufline;
	    else 
		cc_list.cc_list_head = tmp_bufline;

	    tmp_bufline->next = tmp_insafter;
	    tmp_insafter->prev = tmp_bufline;
	} else {
	    /* move elements in cc_bufline_ptrs */
	    for (i = cc_list.cc_bufcount; i > buf_idx + 1; --i)
		cc_bufline_ptrs[i] = cc_bufline_ptrs[i - 1];

	    cc_bufline_ptrs[buf_idx + 1] = tmp_bufline;

	    tmp_bufline->next = tmp_insafter->next;
	    if (tmp_bufline->next != NULL)
		tmp_bufline->next->prev = tmp_bufline;

	    tmp_insafter->next = tmp_bufline;
	    tmp_bufline->prev = tmp_insafter;
	}
    }

    /* create an initial compile request for this buffer */
    retval = cc_request_compile(buf);
    if (retval == CC_FAIL)
	return NULL;

    cc_list.cc_list_curr = tmp_bufline;
    cc_list.cc_bufcount++;

    return tmp_bufline;
}

/*
 * Removes the specified buffer from the watch list.
 * TODO: current implementation does not have sorted lists.
 */
    int
cc_rem_buf(buf_T *buf) {
    if (!cc_started)
	return CC_FAIL;

    cc_bufline_T    *tmp_bufline    = NULL;
    int		    buf_idx;
    int		    put_before;
    int		    i;

    tmp_bufline = cc_locate_buf_bin(buf, &buf_idx, &put_before, CC_FOR_REM);
    
    if (tmp_bufline == NULL)
	return CC_NOSUCHBUF;

    if (tmp_bufline->prev != NULL)
	tmp_bufline->prev->next = tmp_bufline->next;

    if (tmp_bufline->next != NULL)
        tmp_bufline->next->prev = tmp_bufline->prev;

    if (tmp_bufline->buf_ewlist_head != NULL)
	cc_free_ewlist(tmp_bufline->buf_ewlist_head);

    vim_free(tmp_bufline);

    /* update cc_bufline_ptrs */
    for (i = buf_idx; i < cc_list.cc_bufcount; ++i)
	cc_bufline_ptrs[i] = cc_bufline_ptrs[i + 1];

    cc_list.cc_bufcount--;

    /* update the screen in case there are some highlighted lines. */
    cc_update_screen();

    return CC_SUCCESS;
}

/*
 * Returns a pointer to the specified buffer node.
 * Returns NULL if there is no such buffer.
 * This is the linear search version.
 * THIS SHOULD NOT BE USED ANYMORE!!!
 */
    static cc_bufline_T *
cc_locate_buf(buf_T *buf) {
    cc_bufline_T    *tmp_bufline    = NULL;
    char_u	    *ffname	    = NULL;

    ffname = buf->b_ffname;

    for (tmp_bufline = cc_list.cc_list_head;
	    tmp_bufline != NULL; tmp_bufline = tmp_bufline->next) {
	if (strcmp((char *) tmp_bufline->buf_name, (char *) ffname))
	    continue;
	else
	    break;
    }

    return tmp_bufline;
}

/*
 * Binary search of the buffer list. Has two modes:
 *  - search for addition (find the correct position in the list which the
 *	specified buffer should be inserted to)(return CC_BUFEXISTS if the 
 *	buffer is already in the watch list),
 *  - search for removal (find the exact location of the buffer, return NULL
 *	if could not be found).
 */
    static cc_bufline_T *
cc_locate_buf_bin(buf_T *buf, int *buf_idx, int *put_before, int mode) {
    cc_bufline_T    *tmp_bufline    = NULL;
    int		    start	    = 0;
    int		    end		    = cc_list.cc_bufcount - 1;
    int		    mid		    = (start + end) / 2;
    int		    retval;

    while (start <= end) {
	mid = (start + end) / 2;

	/* since this is a linked list we are using cc_bufline_ptrs
	 * to find the correct node. */
	tmp_bufline = cc_bufline_ptrs[mid];

	retval = strcmp((char *) buf->b_ffname, (char *) tmp_bufline->buf_name);

	if (retval == 0) {
	    *buf_idx = mid;
	    if (mode == CC_FOR_ADD)
		return NULL;
	    else if (mode == CC_FOR_REM || mode == CC_FOR_FIND)
		return tmp_bufline;
	} else if (retval < 0) {
	    end = mid - 1;
	    *put_before = TRUE;
	} else {
	    start = mid + 1;
	    *put_before = FALSE;
	}
    }

    /* the buffer is not inside the list */
    *buf_idx = mid;
    if (mode == CC_FOR_ADD)
	return tmp_bufline;
    else if (mode == CC_FOR_REM || mode == CC_FOR_FIND)
	return NULL;
    else /* should not be reached. */
	return NULL;
}

/*
 * Frees the specified error & warning list.
 */
    static void
cc_free_ewlist(cc_ewline_T *ewlist_head) {
    if (ewlist_head == NULL)
	return;

    cc_ewline_T	*tmp_ewline = ewlist_head;
    cc_ewline_T *tmp_ewlinep = ewlist_head;

    while (tmp_ewline != NULL) {
	tmp_ewline = tmp_ewline->next;
	free(tmp_ewlinep);
	tmp_ewlinep = tmp_ewline;
    }
}

/*
 * Frees the specified buffer watch list.
 * TODO: CHECK THIS BEHAVIOR!!! IMPORTANT!!!
 */
    static void
cc_free_buflist(cc_info_T *cc_list_a, 
	cc_bufline_T *cc_bufline_ptrs_a[MAX_BUFLINES]) {
    int	    i;

    if (cc_list_a->cc_list_head == NULL)
	return;

    cc_bufline_T *tmp_bufline = cc_list_a->cc_list_head;
    cc_bufline_T *tmp_buflinep = cc_list_a->cc_list_head;
    
    while (tmp_bufline != NULL) {
	cc_free_ewlist(tmp_bufline->buf_ewlist_head);

	tmp_bufline = tmp_bufline->next;
	free(tmp_buflinep);
	tmp_buflinep = tmp_bufline;
    }
    
    for (i = 0; i < cc_list_a->cc_bufcount; ++i)
	cc_bufline_ptrs_a[i] = NULL;

    cc_list_a->cc_list_head = NULL;
    cc_list_a->cc_list_curr = NULL;
    cc_list_a->cc_bufcount = 0;
}

/*
 * Frees the memory.
 */
    static void
cc_free(void) {
    if (!cc_started)
	return;

    cc_list.cc_list_curr = NULL;
    cc_free_buflist(&cc_list, cc_bufline_ptrs);
}

/*
 * TODO: cc_exit() function.
 *	what else should it do???
 */
    void
cc_exit(void) {
    p_ut = old_p_ut;
    cc_started = FALSE;
    cc_free();
}

