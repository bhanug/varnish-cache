/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Storage method based on umem_alloc(3MALLOC)
 */

#include "config.h"

#ifdef HAVE_LIBUMEM

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <umem.h>

#include "cache/cache.h"
#include "cache/cache_obj.h"
#include "storage/storage.h"
#include "storage/storage_simple.h"

static size_t			smu_max = SIZE_MAX;
static MTX			smu_mtx;

struct smu {
	struct storage		s;
	size_t			sz;
};

static struct storage *
smu_alloc(const struct stevedore *st, size_t size)
{
	struct smu *smu;

	Lck_Lock(&smu_mtx);
	VSC_C_main->sma_nreq++;
	if (VSC_C_main->sma_nbytes + size > smu_max)
		size = 0;
	else {
		VSC_C_main->sma_nobj++;
		VSC_C_main->sma_nbytes += size;
		VSC_C_main->sma_balloc += size;
	}
	Lck_Unlock(&smu_mtx);

	if (size == 0)
		return (NULL);

	smu = umem_zalloc(sizeof *smu, UMEM_DEFAULT);
	if (smu == NULL)
		return (NULL);
	smu->sz = size;
	smu->s.priv = smu;
	smu->s.ptr = umem_alloc(size, UMEM_DEFAULT);
	XXXAN(smu->s.ptr);
	smu->s.len = 0;
	smu->s.space = size;
	smu->s.fd = -1;
	smu->s.stevedore = st;
	smu->s.magic = STORAGE_MAGIC;
	return (&smu->s);
}

static void
smu_free(struct storage *s)
{
	struct smu *smu;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	smu = s->priv;
	assert(smu->sz == smu->s.space);
	Lck_Lock(&smu_mtx);
	VSC_C_main->sma_nobj--;
	VSC_C_main->sma_nbytes -= smu->sz;
	VSC_C_main->sma_bfree += smu->sz;
	Lck_Unlock(&smu_mtx);
	umem_free(smu->s.ptr, smu->s.space);
	umem_free(smu, sizeof *smu);
}

static void
smu_trim(const struct storage *s, size_t size)
{
	struct smu *smu;
	void *p;

	CHECK_OBJ_NOTNULL(s, STORAGE_MAGIC);
	smu = s->priv;
	assert(smu->sz == smu->s.space);
	if ((p = umem_alloc(size, UMEM_DEFAULT)) != NULL) {
		memcpy(p, smu->s.ptr, size);
		umem_free(smu->s.ptr, smu->s.space);
		Lck_Lock(&smu_mtx);
		VSC_C_main->sma_nbytes -= (smu->sz - size);
		VSC_C_main->sma_bfree += smu->sz - size;
		smu->sz = size;
		Lck_Unlock(&smu_mtx);
		smu->s.ptr = p;
		smu->s.space = size;
	}
}

static void
smu_init(struct stevedore *parent, int ac, char * const *av)
{
	const char *e;
	uintmax_t u;

	(void)parent;

	AZ(av[ac]);
	if (ac > 1)
		ARGV_ERR("(-sumem) too many arguments\n");

	if (ac == 0 || *av[0] == '\0')
		 return;

	e = VNUM_2bytes(av[0], &u, 0);
	if (e != NULL)
		ARGV_ERR("(-sumem) size \"%s\": %s\n", av[0], e);
	if ((u != (uintmax_t)(size_t)u))
		ARGV_ERR("(-sumem) size \"%s\": too big\n", av[0]);
	smu_max = u;
}

static void
smu_open(const struct stevedore *st)
{
	(void)st;
	AZ(pthread_mutex_init(&smu_mtx, NULL));
}

const struct stevedore smu_stevedore = {
	.magic		=	STEVEDORE_MAGIC,
	.name		=	"umem",
	.init		=	smu_init,
	.open		=	smu_open,
	.alloc		=	smu_alloc,
	.free		=	smu_free,
	.trim		=	smu_trim,
	.allocobj	=	SML_allocobj,
	.panic		=	SML_panic,
	.methods	=	&SML_methods,
};

#endif /* HAVE_UMEM_H */
