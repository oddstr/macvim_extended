/* vim:set ts=8 sts=4 sw=4 tw=0: */
/*
 * wordbuf.h -
 *
 * Written By:  MURAOKA Taro <koron@tka.att.ne.jp>
 * Last Change: 04-May-2004.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wordbuf.h"

#define WORDLEN_DEF 64

int n_wordbuf_open = 0;		/* for DEBUG */
int n_wordbuf_close = 0;	/* for DEBUG */

/* function pre-declaration */
static int wordbuf_extend(wordbuf_p p, int len);

    wordbuf_p
wordbuf_open()
{
    wordbuf_p p = (wordbuf_p)malloc(sizeof(wordbuf_t));

    if (p)
    {
	++n_wordbuf_open;	/* for DEBUG */
	p->len = WORDLEN_DEF;
	p->buf = (unsigned char*)malloc(p->len);
	p->last = 0;
	p->buf[0] = '\0';
    }
    return p;
}

    void
wordbuf_close(wordbuf_p p)
{
    if (p)
    {
	++n_wordbuf_close;	/* for DEBUG */
	free(p->buf);
	free(p);
    }
}

    void
wordbuf_reset(wordbuf_p p)
{
    p->last = 0;
    p->buf[0] = '\0';
}

/*
 * wordbuf_extend(wordbuf_p p, int req_len);
 *	�o�b�t�@�̐L���B�G���[���ɂ�0���A��B
 *	�������̂��߂ɐL�΂��ׂ����͌ďo���Ŕ��f����B
 */
    static int
wordbuf_extend(wordbuf_p p, int req_len)
{
    int newlen = p->len * 2;
    unsigned char *newbuf;

    while (req_len > newlen)
	newlen *= 2;
    if (!(newbuf = (unsigned char*)realloc(p->buf, newlen)))
    {
	/*fprintf(stderr, "wordbuf_add(): failed to extend buffer\n");*/
	return 0;
    }
    else
    {
	p->len = newlen;
	p->buf = newbuf;
	return req_len;
    }
}

    int
wordbuf_last(wordbuf_p p)
{
    return p->last;
}

    int
wordbuf_add(wordbuf_p p, unsigned char ch)
{
    int newlen = p->last + 2;

    if (newlen > p->len && !wordbuf_extend(p, newlen))
	return 0;
    else
    {
#if 1
	unsigned char *buf = p->buf + p->last;

	buf[0] = ch;
	buf[1] = '\0';
#else
	/* ���g���G���f�B�A�������肷��Ȃ�g���邪�c */
	*(unsigned short*)&p->buf[p->last] = (unsigned short)ch;
#endif
	return ++p->last;
    }
}

    int
wordbuf_cat(wordbuf_p p, const unsigned char* sz)
{
    int len;

    if (sz && (len = strlen(sz)) > 0)
    {
	int newlen = p->last + len + 1;

	if (newlen > p->len && !wordbuf_extend(p, newlen))
	    return 0;
	memcpy(&p->buf[p->last], sz, len + 1);
	p->last = p->last + len;
    }
    return p->last;
}

    unsigned char*
wordbuf_get(wordbuf_p p)
{
    return p->buf;
}
