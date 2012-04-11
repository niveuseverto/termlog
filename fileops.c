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
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/snoop.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/filio.h>

#include <utmpx.h>
#include <signal.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <paths.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <sha.h>

#include "utmp.h"
#include "termlog.h"
#include "fileops.h"

int maxfsize = 0;
int appendonly = 0;

static char *
timestamp(void)
{
	static char buf[32];
	struct timeval tv;
	struct tm *tm;
	register int s;
	char date[16];

	gettimeofday(&tv, 0);
	tm = localtime((time_t *)&tv.tv_sec);
	strftime(date, sizeof(date) - 1, "%Y-%m-%d", tm);
	s = tv.tv_sec % 86400;
	snprintf(buf, sizeof(buf) - 1,
	    "%s %02d:%02d:%02d.%06u",
	    date, (s / 3600), ((s % 3600) / 60),
	    (s % 60), ((u_int32_t)tv.tv_usec));
	return (&buf[0]);
}

void *
snp_setup(void *m_data, char *config __unused)
{
	struct snpmeta *sm;
	struct snp_d *snp;
	char logname[256];

	assert(m_data != NULL);
	sm = malloc(sizeof(struct snpmeta));
	if (sm == NULL)
		return (NULL);
	snp = (struct snp_d *)m_data;
	snprintf(logname, sizeof(logname) - 1,
	    "%s_%s_%d.log", snp->s_username,
	    snp->s_line, time(0));
	while(index(logname,'/')) *(index(logname,'/')) = '_';
	sm->fp = fopen(logname, "w");
	if (sm->fp == NULL)
		return (NULL);
	sm->unit = 2;
	sm->counter = 0;
	dolog("%s session %s created", timestamp(), logname);
	strlcpy(sm->fname, logname, sizeof(sm->fname));
	if (chmod(logname, S_IWUSR | S_IRUSR) < 0)
		warn("chmod failed");
	if (appendonly)
		if (chflags(sm->fname, SF_APPEND) < 0)
			warn("chflags failed");
	fprintf(sm->fp,
	    ";; Session started: %s\n"
	    ";; Username: %s\n"
	    ";; TTY line: %s\n",
	    timestamp(), snp->s_username,
	    snp->s_line);
	return (sm);
}

int
snp_overflow(void *m_data)
{
	struct snpmeta *sm;

	sm = (struct snpmeta *)m_data;
	fprintf(sm->fp,
	    "\n;; %s TTY overflow: Possibly missing data\n\n",
	    timestamp());
	return (0);
}

int
snp_remove(void *m_data)
{
	struct snpmeta *sm;

	assert(m_data != NULL);
	sm = (struct snpmeta *)m_data;
	fprintf(sm->fp, "\n;; Session closed: %s\n", timestamp());
	fclose(sm->fp);
	log_message_digest(sm);
	free(sm);
	return (0);
}

int
snp_write_log(void *m_data, char *ptr, int size)
{
	struct snpmeta *sm;
	char fname[MAXPATHLEN];

	assert(m_data != NULL || ptr != NULL);
	sm = (struct snpmeta *)m_data;
	if (maxfsize > 0 && ftell(sm->fp) > maxfsize) {
		fclose(sm->fp);
		log_message_digest(sm);
		sprintf(fname, "%s%d", sm->fname, sm->unit++);
		sm->fp = fopen(fname, "w");
		if (sm->fp == NULL) 
			err(1, "fopen %s failed", fname);
		if (chmod(fname, S_IWUSR | S_IRUSR) < 0)
			err(1, "chmod failed");
		if (appendonly)
			if (chflags(fname, SF_APPEND) < 0)
				err(1, "chflags failed");
	}
	fwrite(ptr, size, 1, sm->fp);
	sm->counter += size;
	fflush(sm->fp);
	return (0);
}

int
log_message_digest(struct snpmeta *sm)
{
	char fname[MAXPATHLEN], *f, *hash;

	if (sm->unit != 2) {
		(void)snprintf(fname, sizeof(fname) - 1,
		    "%s%d", sm->fname, sm->unit - 1);
		f = &fname[0];
	} else
		f = &sm->fname[0];
	hash = SHA1_File(f, 0);
	if (hash == NULL) {
		warnx("digest calculation failed");
		return (1);
	}
	dolog("%s session %s closed sha1 checksum %s bytes logged %qu",
	    timestamp(), f, hash, sm->counter);
	free(hash);
	return (0);
}
