/* vim:set ts=8 sts=4 sw=4 tw=0: */
/*
 * wordlist.h -
 *
 * Written By:  MURAOKA Taro <koron@tka.att.ne.jp>
 * Last Change: 04-May-2004.
 */

#include <stdlib.h>
#include <string.h>
#include "wordlist.h"

int n_wordlist_open	= 0;
int n_wordlist_close	= 0;
int n_wordlist_total	= 0;

    wordlist_p
wordlist_open_len(const unsigned char* ptr, int len)
{
    if (ptr && len >= 0)
    {
	wordlist_p p;

	if ((p = (wordlist_p)malloc(sizeof(*p) + len + 1)) != NULL)
	{
	    p->ptr  = (char*)(p + 1);
	    p->next = NULL;
	    /*
	     * �ق�strdup()�Ɠ����Ȏ����B�P��̕ۑ��ɕK�v�ȑ���������m�肽
	     * ���̂œƎ��ɍĎ����B
	     */
	    memcpy(p->ptr, ptr, len);
	    p->ptr[len] = '\0';

	    ++n_wordlist_open;
	    n_wordlist_total += len;
	}
	return p;
    }
    return NULL;
}

    wordlist_p
wordlist_open(const unsigned char* ptr)
{
    return ptr ? wordlist_open_len(ptr, strlen(ptr)) : NULL;
}

    void
wordlist_close(wordlist_p p)
{
    while (p)
    {
	wordlist_p next = p->next;

	++n_wordlist_close;
	free(p);
	p = next;
    }
}
