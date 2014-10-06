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

#ifndef __VIVANTE_GPU_H__
#define __VIVANTE_GPU_H__

#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include "vivante_drv.h"
#include "vivante_ringbuffer.h"

struct msm_gem_submit;

/* So far, with hardware that I've seen to date, we can have:
 *  + zero, one, or two z180 2d cores
 *  + a3xx or a2xx 3d core, which share a common CP (the firmware
 *    for the CP seems to implement some different PM4 packet types
 *    but the basics of cmdstream submission are the same)
 *
 * Which means that the eventual complete "class" hierarchy, once
 * support for all past and present hw is in place, becomes:
 *  + msm_gpu
 *    + adreno_gpu
 *      + a3xx_gpu
 *      + a2xx_gpu
 *    + z180_gpu
 */
struct vivante_gpu_funcs {
	int (*get_param)(struct vivante_gpu *gpu, uint32_t param, uint64_t *value);
	int (*hw_init)(struct vivante_gpu *gpu);
	int (*pm_suspend)(struct vivante_gpu *gpu);
	int (*pm_resume)(struct vivante_gpu *gpu);
	int (*submit)(struct vivante_gpu *gpu, struct msm_gem_submit *submit,
			struct vivante_file_private *ctx);
	void (*flush)(struct vivante_gpu *gpu);
	void (*idle)(struct vivante_gpu *gpu);
	irqreturn_t (*irq)(struct vivante_gpu *irq);
	uint32_t (*last_fence)(struct vivante_gpu *gpu);
	void (*recover)(struct vivante_gpu *gpu);
	void (*destroy)(struct vivante_gpu *gpu);
#ifdef CONFIG_DEBUG_FS
	/* show GPU status in debugfs: */
	void (*show)(struct vivante_gpu *gpu, struct seq_file *m);
#endif
};

struct vivante_chip_identity
{
	/* Chip model. */
	uint32_t model;

	/* Revision value.*/
	uint32_t revision;

	/* Supported feature fields. */
	uint32_t features;

	/* Supported minor feature fields. */
	uint32_t minor_features;

	/* Supported minor feature 1 fields. */
	uint32_t minor_features1;

	/* Supported minor feature 2 fields. */
	uint32_t minor_features2;

	/* Supported minor feature 3 fields. */
	uint32_t minor_features3;

	/* Number of streams supported. */
	uint32_t stream_count;

	/* Total number of temporary registers per thread. */
	uint32_t register_max;

	/* Maximum number of threads. */
	uint32_t thread_count;

	/* Number of shader cores. */
	uint32_t shader_core_count;

	/* Size of the vertex cache. */
	uint32_t vertex_cache_size;

	/* Number of entries in the vertex output buffer. */
	uint32_t vertex_output_buffer_size;

	/* Number of pixel pipes. */
	uint32_t pixel_pipes;

	/* Number of instructions. */
	uint32_t instruction_count;

	/* Number of constants. */
	uint32_t num_constants;

	/* Buffer size */
	uint32_t buffer_size;
};

struct vivante_gpu {
	const char *name;
	struct drm_device *dev;
	const struct vivante_gpu_funcs *funcs;
	struct vivante_chip_identity identity;

	struct vivante_ringbuffer *rb;
	uint32_t rb_iova;

	/* list of GEM active objects: */
	struct list_head active_list;

	uint32_t submitted_fence;

	/* worker for handling active-list retiring: */
	struct work_struct retire_work;

	void __iomem *mmio;
	int irq;

	struct vivante_iommu *mmu;
	int id;

	/* Power Control: */
	struct regulator *gpu_reg, *gpu_cx;
	struct clk *grp_clks[6];
	uint32_t fast_rate, slow_rate, bus_freq;

	/* Hang Detction: */
#define DRM_MSM_HANGCHECK_PERIOD 500 /* in ms */
#define DRM_MSM_HANGCHECK_JIFFIES msecs_to_jiffies(DRM_MSM_HANGCHECK_PERIOD)
	struct timer_list hangcheck_timer;
	uint32_t hangcheck_fence;
	struct work_struct recover_work;
};

static inline void gpu_write(struct vivante_gpu *gpu, u32 reg, u32 data)
{
	vivante_writel(data, gpu->mmio + reg);
}

static inline u32 gpu_read(struct vivante_gpu *gpu, u32 reg)
{
	return vivante_readl(gpu->mmio + reg);
}

struct vivante_gpu *vivante_gpu_init(struct drm_device *dev,const char *name,
		const char *ioname, const char *irqname);

int msm_gpu_pm_suspend(struct vivante_gpu *gpu);
int msm_gpu_pm_resume(struct vivante_gpu *gpu);

void msm_gpu_retire(struct vivante_gpu *gpu);
int msm_gpu_submit(struct vivante_gpu *gpu, struct msm_gem_submit *submit,
		struct vivante_file_private *ctx);

int msm_gpu_init(struct drm_device *drm, struct vivante_gpu *gpu,
		const struct vivante_gpu_funcs *funcs, const char *name,
		const char *ioname, const char *irqname);
void msm_gpu_cleanup(struct vivante_gpu *gpu);

#endif /* __VIVANTE_GPU_H__ */
