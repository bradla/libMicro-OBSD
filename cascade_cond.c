/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms
 * of the Common Development and Distribution License
 * (the "License").  You may not use this file except
 * in compliance with the License.
 *
 * You can obtain a copy of the license at
 * src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL
 * HEADER in each file and include the License file at
 * usr/src/OPENSOLARIS.LICENSE.  If applicable,
 * add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your
 * own identifying information: Portions Copyright [yyyy]
 * [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * The "cascade" test case is a multiprocess/multithread batten-passing model
 * using lock primitives alone for synchronisation. Threads are arranged in a
 * ring. Each thread has two locks of its own on which it blocks, and is able
 * to manipulate the two locks belonging to the thread which follows it in the
 * ring.
 *
 * The number of threads (nthreads) is specified by the generic libMicro -P/-T
 * options. With nthreads == 1 (the default) the uncontended case can be timed.
 *
 * The main logic is generic and allows any simple blocking API to be tested.
 * The API-specific component is clearly indicated.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>

#include "libmicro.h"

typedef struct {
	int			ts_once;
	int			ts_id;
	int			ts_us0;		/* our lock indices */
	int			ts_us1;
	int			ts_them0;	/* their lock indices */
	int			ts_them1;
} tsd_t;

static int			nthreads;

/*
 * API-specific code BEGINS here
 */

static int			opto = 0;
static int			opts = 0;
static int			nlocks;
static pthread_mutex_t	*mxs;
static pthread_cond_t		*cvs;
static int			*conds;

int
benchmark_init()
{
	lm_tsdsize = sizeof (tsd_t);

	(void) sprintf(lm_optstr, "os");

	lm_defN = "cscd_cond";

	(void) sprintf(lm_usage,
	    "       [-o] (do signal outside mutex)\n"
	    "       [-s] (force PTHREAD_PROCESS_SHARED)\n"
	    "notes: thread cascade using pthread_conds\n");

	return (0);
}

/*ARGSUSED*/
int
benchmark_optswitch(int opt, char *optarg)
{
	switch (opt) {
	case 'o':
		opto = 1;
		break;
	case 's':
		opts = 1;
		break;
	default:
		return (-1);
	}
	return (0);
}

int
benchmark_initrun()
{
	int			i;
	int			e = 0;
	pthread_mutexattr_t	ma;
	pthread_condattr_t	ca;

	nthreads = lm_optP * lm_optT;
	nlocks = nthreads * 2;
	/*LINTED*/
	mxs = (pthread_mutex_t *)mmap(NULL,
	    nlocks * sizeof (pthread_mutex_t),
	    PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED,
	    -1, 0L);
	if (mxs == MAP_FAILED) {
		return (1);
	}

	/*LINTED*/
	cvs = (pthread_cond_t *)mmap(NULL,
	    nlocks * sizeof (pthread_cond_t),
	    PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED,
	    -1, 0L);
	if (cvs == MAP_FAILED) {
		return (1);
	}

	/*LINTED*/
	conds = (int *)mmap(NULL,
	    nlocks * sizeof (pthread_cond_t),
	    PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED,
	    -1, 0L);
	if (conds == MAP_FAILED) {
		return (1);
	}

	(void) pthread_mutexattr_init(&ma);
	(void) pthread_condattr_init(&ca);

#if !defined(__OpenBSD__)
	if (lm_optP > 1 || opts) {
		(void) pthread_mutexattr_setpshared(&ma,
		    PTHREAD_PROCESS_SHARED);
		(void) pthread_condattr_setpshared(&ca,
		    PTHREAD_PROCESS_SHARED);
	} else {
		(void) pthread_mutexattr_setpshared(&ma,
		    PTHREAD_PROCESS_PRIVATE);
		(void) pthread_condattr_setpshared(&ca,
		    PTHREAD_PROCESS_PRIVATE);
	}
#endif

	for (i = 0; i < nlocks; i++) {
		(void) pthread_mutex_init(&mxs[i], &ma);
		(void) pthread_cond_init(&cvs[i], &ca);
		conds[i] = 0;
	}

	return (e);
}

int
block(int index)
{
	(void) pthread_mutex_lock(&mxs[index]);
	while (conds[index] != 0) {
		(void) pthread_cond_wait(&cvs[index], &mxs[index]);
	}
	conds[index] = 1;
	(void) pthread_mutex_unlock(&mxs[index]);

	return (0);
}

int
unblock(int index)
{
	(void) pthread_mutex_lock(&mxs[index]);
	conds[index] = 0;
	if (opto) {
		(void) pthread_mutex_unlock(&mxs[index]);
		(void) pthread_cond_signal(&cvs[index]);
	} else {
		(void) pthread_cond_signal(&cvs[index]);
		(void) pthread_mutex_unlock(&mxs[index]);
	}
	return (0);
}

/*
 * API-specific code ENDS here
 */

int
benchmark_initbatch(void *tsd)
{
	tsd_t			*ts = (tsd_t *)tsd;
	int			e = 0;

	if (ts->ts_once == 0) {
		int		us, them;

		us = (getpindex() * lm_optT) + gettindex();
		them = (us + 1) % (lm_optP * lm_optT);

		ts->ts_id = us;

		/* lock index asignment for us and them */
		ts->ts_us0 = (us * 2);
		ts->ts_us1 = (us * 2) + 1;
		if (us < nthreads - 1) {
			/* straight-thru connection to them */
			ts->ts_them0 = (them * 2);
			ts->ts_them1 = (them * 2) + 1;
		} else {
			/* cross-over connection to them */
			ts->ts_them0 = (them * 2) + 1;
			ts->ts_them1 = (them * 2);
		}

		ts->ts_once = 1;
	}

	/* block their first move */
	e += block(ts->ts_them0);

	return (e);
}

int
benchmark(void *tsd, result_t *res)
{
	tsd_t			*ts = (tsd_t *)tsd;
	int			i;
	int			e = 0;

	/* wait to be unblocked (id == 0 will not block) */
	e += block(ts->ts_us0);

	for (i = 0; i < lm_optB; i += 2) {
		/* allow them to block us again */
		e += unblock(ts->ts_us0);

		/* block their next + 1 move */
		e += block(ts->ts_them1);

		/* unblock their next move */
		e += unblock(ts->ts_them0);

		/* wait for them to unblock us */
		e += block(ts->ts_us1);

		/* repeat with locks reversed */
		e += unblock(ts->ts_us1);
		e += block(ts->ts_them0);
		e += unblock(ts->ts_them1);
		e += block(ts->ts_us0);
	}

	/* finish batch with nothing blocked */
	e += unblock(ts->ts_them0);
	e += unblock(ts->ts_us0);

	res->re_count = i;
	res->re_errors = e;

	return (0);
}
