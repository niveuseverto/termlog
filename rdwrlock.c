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
#include <pthread.h>
#include <stdio.h>
#include <assert.h>

#include "rdwrlock.h"

void
rdwr_lock_init(struct rdwrlock *rwp)
{
#ifdef DEBUG_LOGS
	DEBUG("initializing rw lock %p", rwp);
#endif
	rwp->lock = PTHREAD_MUTEX_INITIALIZER;
	rwp->rdsok = PTHREAD_COND_INITIALIZER;
	rwp->wrok = PTHREAD_COND_INITIALIZER;
	rwp->rwlock = 0;
	rwp->nwrs_wait = 0;
	rwp->thrid = 0;
}

void
rd_lock(struct rdwrlock *rwp)
{
#ifdef DEBUG_LOGS
	DEBUG("aquiring read lock %p", rwp);
#endif
	assert(rwp != NULL);
	pthread_mutex_lock(&rwp->lock);
	while (rwp->rwlock < 0 || rwp->nwrs_wait)
		pthread_cond_wait(&rwp->rdsok, &rwp->lock);
	rwp->rwlock++;
	pthread_mutex_unlock(&rwp->lock);
}

void
wr_lock(struct rdwrlock *rwp)
{
#ifdef DEBUG_LOGS
	DEBUG("aquiring write lock %p", rwp);
#endif
	assert(rwp != NULL);
	pthread_mutex_lock(&rwp->lock);
	rwp->thrid = pthread_self();
	while (rwp->rwlock != 0) {
		rwp->nwrs_wait++;
		pthread_cond_wait(&rwp->wrok, &rwp->lock);
		rwp->nwrs_wait--;
	}
	rwp->rwlock = -1;
	pthread_mutex_unlock(&rwp->lock);
}

void
rdwr_unlock(struct rdwrlock *rwp)
{
#ifdef DEBUG_LOGS
	DEBUG("unlocking rw lock %p", rwp);
#endif
	assert(rwp != NULL);
	pthread_mutex_lock(&rwp->lock);
	if (rwp->rwlock < 0)
		rwp->rwlock = 0;
	else
		rwp->rwlock--;
	if (rwp->thrid != 0)
		rwp->thrid = 0;
	if (rwp->nwrs_wait && rwp->rwlock == 0) {
		pthread_mutex_unlock(&rwp->lock);
		pthread_cond_signal(&rwp->wrok);
	} else if (rwp->nwrs_wait == 0) {
		pthread_mutex_unlock(&rwp->lock);
		pthread_cond_broadcast(&rwp->rdsok);
	}
}
