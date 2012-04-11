/*-
 * Copyright (c) 2002-2005 Christian S.J. Peron
 * Copyright (c) 2002-2005 Seccuris Labs, Inc
 * All rights reserved.
 *
 * This software was developed for Seccuris Labs by Christian S.J. Peron.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef	__RDWRLOCK_DOT_H__
#define	__RDWRLOCK_DOT_H__
#include <pthread.h>

struct rdwrlock {
	pthread_mutex_t		lock;
	int			rwlock;
	pthread_cond_t		rdsok;
	int		nwrs_wait;
	pthread_cond_t		wrok;
	pthread_t		thrid;
};
#define	M_OWNED		0x0000001U
#define	M_NOTOWNED	0x0000002U
#define	WRLOCK_ASSERT(what, thread) do {				\
	pthread_t me;							\
	me = pthread_self();						\
	if (what == M_OWNED)						\
		assert((thread)->thrid == me);				\
	else if (what == M_NOTOWNED)					\
		assert((thread)->thrid != me);				\
} while (0)
void rdwr_lock_init(struct rdwrlock *);
void rd_lock(struct rdwrlock *);
void wr_lock(struct rdwrlock *);
void rdwr_unlock(struct rdwrlock *);
#endif	/* __RDWRLOCK_DOT_H__ */
