/*
 * Copyright (C) 2017 Etnaviv Project
 * Copyright (C) 2017 Zodiac Inflight Innovations
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "etnaviv_gpu.h"
#include "etnaviv_perfmon.h"

struct etnaviv_pm_domain;

struct etnaviv_pm_signal {
	char name[64];
	u32 data;

	u32 (*sample)(struct etnaviv_gpu *gpu,
	              const struct etnaviv_pm_domain *domain,
	              const struct etnaviv_pm_signal *signal);
};

struct etnaviv_pm_domain {
	char name[64];
	u8 nr_signals;
	const struct etnaviv_pm_signal *signal;
};

static const struct etnaviv_pm_domain doms[] = {
};

int etnaviv_pm_query_dom(struct etnaviv_gpu *gpu,
	struct drm_etnaviv_pm_domain *domain)
{
	const struct etnaviv_pm_domain *dom;

	if (domain->iter >= ARRAY_SIZE(doms))
		return -EINVAL;

	dom = &doms[domain->iter];

	domain->id = domain->iter;
	domain->nr_signals = dom->nr_signals;
	strncpy(domain->name, dom->name, sizeof(domain->name));

	domain->iter++;
	if (domain->iter == ARRAY_SIZE(doms))
		domain->iter = 0xff;

	return 0;
}

int etnaviv_pm_query_sig(struct etnaviv_gpu *gpu,
	struct drm_etnaviv_pm_signal *signal)
{
	const struct etnaviv_pm_domain *dom;
	const struct etnaviv_pm_signal *sig;

	if (signal->domain >= ARRAY_SIZE(doms))
		return -EINVAL;

	dom = &doms[signal->domain];

	if (signal->iter > dom->nr_signals)
		return -EINVAL;

	sig = &dom->signal[signal->iter];

	signal->id = signal->iter;
	strncpy(signal->name, sig->name, sizeof(signal->name));

	signal->iter++;
	if (signal->iter == dom->nr_signals)
		signal->iter = 0xffff;

	return 0;
}

int etnaviv_pm_req_validate(const struct drm_etnaviv_gem_submit_pmr *r)
{
	const struct etnaviv_pm_domain *dom;

	if (r->domain >= ARRAY_SIZE(doms))
		return -EINVAL;

	dom = &doms[r->domain];

	if (r->signal > dom->nr_signals)
		return -EINVAL;

	return 0;
}

void etnaviv_perfmon_process(struct etnaviv_gpu *gpu,
	const struct etnaviv_perfmon_request *pmr)
{
	const struct etnaviv_pm_domain *dom;
	const struct etnaviv_pm_signal *sig;
	u32 *bo = pmr->bo_vma;
	u32 val;

	dom = &doms[pmr->domain];
	sig = &dom->signal[pmr->signal];
	val = sig->sample(gpu, dom, sig);

	*(bo + pmr->offset) = val;
}
