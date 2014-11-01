/*
 * Copyright (C) 2014 2014 Etnaviv Project
 * Author: Christian Gmeiner <christian.gmeiner@gmail.com>
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
#include "etnaviv_gem.h"

#include "common.xml.h"
#include "state.xml.h"
#include "cmdstream.xml.h"

/*
 * Command Buffer helper:
 */

#define CMD_LINK_NUM_WORDS (2 + 1)

static inline u32 to_bytes(u32 words)
{
	return words * 4;
}

static inline void OUT(struct etnaviv_gem_object *buffer, uint32_t data)
{
	u32 *vaddr = (u32 *)buffer->vaddr;
	BUG_ON(to_bytes(buffer->offset) >= buffer->base.size);

	vaddr[buffer->offset++] = data;
}

static inline void buffer_reserve(struct etnaviv_gem_object *buffer, u32 size)
{
	buffer->offset = ALIGN(buffer->offset, 2);

	if (!buffer->is_ring_buffer)
		return;

	if (to_bytes(buffer->offset + size + CMD_LINK_NUM_WORDS) <= buffer->base.size)
		return;

	/* jump to the start of the buffer */
	OUT(buffer, VIV_FE_LINK_HEADER_OP_LINK | VIV_FE_LINK_HEADER_PREFETCH(0xffffffff /* TODO */));
	OUT(buffer, buffer->paddr);
	buffer->offset = 0;
}

static inline void CMD_LOAD_STATE(struct etnaviv_gem_object *buffer, u32 reg, u32 value)
{
	buffer_reserve(buffer, 2);

	/* write a register via cmd stream */
	OUT(buffer, VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE | VIV_FE_LOAD_STATE_HEADER_COUNT(1) |
			VIV_FE_LOAD_STATE_HEADER_OFFSET(reg >> VIV_FE_LOAD_STATE_HEADER_OFFSET__SHR));
	OUT(buffer, value);
}

static inline void CMD_END(struct etnaviv_gem_object *buffer)
{
	buffer_reserve(buffer, 1);

	OUT(buffer, VIV_FE_END_HEADER_OP_END);
}

static inline void CMD_WAIT(struct etnaviv_gem_object *buffer)
{
	buffer_reserve(buffer, 1);

	buffer->last_wait = buffer->vaddr + to_bytes(buffer->offset);
	OUT(buffer, VIV_FE_WAIT_HEADER_OP_WAIT | 200);
}

static inline void CMD_LINK(struct etnaviv_gem_object *buffer, u16 prefetch, u32 address)
{
	buffer_reserve(buffer, 2);

	OUT(buffer, VIV_FE_LINK_HEADER_OP_LINK | VIV_FE_LINK_HEADER_PREFETCH(prefetch));
	OUT(buffer, address);
}

static inline void CMD_STALL(struct etnaviv_gem_object *buffer, u32 from, u32 to)
{
	buffer_reserve(buffer, 2);

	OUT(buffer, VIV_FE_STALL_HEADER_OP_STALL);
	OUT(buffer, VIV_FE_STALL_TOKEN_FROM(from) | VIV_FE_STALL_TOKEN_TO(to));
}

/*
 * High level commands:
 */

static void etnaviv_cmd_select_pipe(struct etnaviv_gem_object *buffer, u8 pipe)
{
	u32 flush;
	u32 stall;

	if (pipe == ETNA_PIPE_2D)
		flush = VIVS_GL_FLUSH_CACHE_DEPTH | VIVS_GL_FLUSH_CACHE_COLOR;
	else
		flush = VIVS_GL_FLUSH_CACHE_TEXTURE;

	stall = VIVS_GL_SEMAPHORE_TOKEN_FROM(SYNC_RECIPIENT_FE) |
			VIVS_GL_SEMAPHORE_TOKEN_TO(SYNC_RECIPIENT_PE);

	CMD_LOAD_STATE(buffer, VIVS_GL_FLUSH_CACHE, flush);
	CMD_LOAD_STATE(buffer, VIVS_GL_SEMAPHORE_TOKEN, stall);

	CMD_STALL(buffer, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

	CMD_LOAD_STATE(buffer, VIVS_GL_PIPE_SELECT, VIVS_GL_PIPE_SELECT_PIPE(pipe));
}

static int etnaviv_cmd_mmu_flush(struct etnaviv_gem_object *buffer)
{
	int words_used = 0;

	if (buffer->gpu->mmuv1) {
		CMD_LOAD_STATE(buffer, VIVS_GL_FLUSH_MMU,
				VIVS_GL_FLUSH_MMU_FLUSH_FEMMU |
				VIVS_GL_FLUSH_MMU_FLUSH_PEMMU);

		words_used = 2;
	} else {
		/* TODO */
	}

	return words_used;
}


u32 etnaviv_buffer_init(struct etnaviv_gpu *gpu)
{
	struct etnaviv_gem_object *buffer = to_etnaviv_bo(gpu->buffer);

	/* initialize buffer */
	buffer->offset = 0;
	buffer->is_ring_buffer = true;
	buffer->gpu = gpu;

	etnaviv_cmd_select_pipe(buffer, gpu->pipe);

	CMD_WAIT(buffer);
	CMD_LINK(buffer, 2, buffer->paddr + to_bytes(buffer->offset - 1));

	return buffer->offset;
}

void etnaviv_buffer_queue(struct etnaviv_gpu *gpu, unsigned int event, struct etnaviv_gem_submit *submit)
{
	struct etnaviv_gem_object *buffer = to_etnaviv_bo(gpu->buffer);
	struct etnaviv_gem_object *cmd = submit->cmd.obj;
	u32 ring_jump, i;
	u32 *fixup, *last_wait;
	u16 prefetch;
	u16 mmu_flush_words = 0;

	/* store start of the new queuing commands */
	ring_jump = buffer->offset;

	/* we need to store the current last_wait locally */
	last_wait = buffer->last_wait;

	if (0 /* TODO */)
		mmu_flush_words = etnaviv_cmd_mmu_flush(buffer);

	/* link to cmd buffer - we need to patch prefetch value later */
	CMD_LINK(buffer, 0, cmd->paddr);

	/* patch cmd buffer */
	cmd->offset = submit->cmd.size;
	CMD_LINK(cmd, 2, buffer->paddr + to_bytes(buffer->offset));

	/* fix prefetch value in 'ring'-buffer */
	prefetch = cmd->offset;
	fixup = (u32 *)buffer->vaddr + buffer->offset - 2;
	*fixup = VIV_FE_LINK_HEADER_OP_LINK | VIV_FE_LINK_HEADER_PREFETCH(prefetch);

	/* trigger event */
	CMD_LOAD_STATE(buffer, VIVS_GL_EVENT,
			VIVS_GL_EVENT_EVENT_ID(event) | VIVS_GL_EVENT_FROM_PE);

	/* append WAIT/LINK to 'ring'-buffer */
	CMD_WAIT(buffer);
	CMD_LINK(buffer, 2, buffer->paddr + to_bytes(buffer->offset - 1));

	/* change WAIT into a LINK command */
	i = VIV_FE_LINK_HEADER_OP_LINK |
			VIV_FE_LINK_HEADER_PREFETCH(2 + mmu_flush_words);

	*(last_wait + 1) = buffer->paddr + to_bytes(ring_jump);
	mb();	/* first make sure the GPU sees the address part */
	*(last_wait) = i;
	mb();	/* followed by the actual LINK opcode */
}
