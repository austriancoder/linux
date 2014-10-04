/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#include "vivante_drv.h"
#include "vivante_mmu.h"

static int vivante_fault_handler(struct iommu_domain *iommu, struct device *dev,
		unsigned long iova, int flags, void *arg)
{
	DBG("*** fault: iova=%08lx, flags=%d", iova, flags);
	return 0;
}

int vivante_iommu_attach(struct vivante_iommu *iommu, const char **names, int cnt)
{
	struct drm_device *dev = iommu->dev;
	int i, ret;

	for (i = 0; i < cnt; i++) {
		struct device *msm_iommu_get_ctx(const char *ctx_name);
		struct device *ctx = msm_iommu_get_ctx(names[i]);
		if (IS_ERR_OR_NULL(ctx))
			continue;
		ret = iommu_attach_device(iommu->domain, ctx);
		if (ret) {
			dev_warn(dev->dev, "could not attach iommu to %s", names[i]);
			return ret;
		}
	}

	return 0;
}

int vivante_iommu_map(struct vivante_iommu *iommu, uint32_t iova,
		struct sg_table *sgt, unsigned len, int prot)
{
	struct iommu_domain *domain = iommu->domain;
	struct scatterlist *sg;
	unsigned int da = iova;
	unsigned int i, j;
	int ret;

	if (!domain || !sgt)
		return -EINVAL;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		u32 pa = sg_phys(sg) - sg->offset;
		size_t bytes = sg->length + sg->offset;

		VERB("map[%d]: %08x %08x(%x)", i, iova, pa, bytes);

		ret = iommu_map(domain, da, pa, bytes, prot);
		if (ret)
			goto fail;

		da += bytes;
	}

	return 0;

fail:
	da = iova;

	for_each_sg(sgt->sgl, sg, i, j) {
		size_t bytes = sg->length + sg->offset;
		iommu_unmap(domain, da, bytes);
		da += bytes;
	}
	return ret;
}

int vivante_iommu_unmap(struct vivante_iommu *iommu, uint32_t iova,
		struct sg_table *sgt, unsigned len)
{
	struct iommu_domain *domain = iommu->domain;
	struct scatterlist *sg;
	unsigned int da = iova;
	int i;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		size_t bytes = sg->length + sg->offset;
		size_t unmapped;

		unmapped = iommu_unmap(domain, da, bytes);
		if (unmapped < bytes)
			return unmapped;

		VERB("unmap[%d]: %08x(%x)", i, iova, bytes);

		BUG_ON(!IS_ALIGNED(bytes, PAGE_SIZE));

		da += bytes;
	}

	return 0;
}

void vivante_iommu_destroy(struct vivante_iommu *mmu)
{
	iommu_domain_free(mmu->domain);
	kfree(mmu);
}

struct vivante_iommu *vivante_iommu_new(struct drm_device *dev, struct iommu_domain *domain)
{
	struct vivante_iommu *mmu;

	mmu = kzalloc(sizeof(*mmu), GFP_KERNEL);
	if (!mmu)
		return ERR_PTR(-ENOMEM);

	mmu->domain = domain;
	mmu->dev = dev;
	iommu_set_fault_handler(domain, vivante_fault_handler, dev);

	return mmu;
}
