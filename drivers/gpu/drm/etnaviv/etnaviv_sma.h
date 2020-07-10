/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Etnaviv Project
 */

#ifndef __ETNAVIV_SMA_H__
#define __ETNAVIV_SMA_H__

#include <linux/bug.h>
#include <linux/compiler.h>

/*
 * Simple moving average (SMA)
 *
 * This implements a fixed-size SMA algorithm.
 *
 * The first argument to the macro is the name that will be used
 * for the struct and helper functions.
 *
 * The second argument, the samples, expresses how many samples are
 * used for the SMA algorithm.
 */

#define DECLARE_SMA(name, _samples) \
    struct sma_##name { \
        unsigned long pos; \
        unsigned long sum; \
        unsigned long samples[_samples]; \
    }; \
    static inline void sma_##name##_init(struct sma_##name *s) \
    { \
        BUILD_BUG_ON(!__builtin_constant_p(_samples));	\
        memset(s, 0, sizeof(struct sma_##name)); \
    } \
    static inline unsigned long sma_##name##_read(struct sma_##name *s) \
    { \
        BUILD_BUG_ON(!__builtin_constant_p(_samples));	\
        return s->sum / _samples; \
    } \
    static inline void sma_##name##_add(struct sma_##name *s, unsigned long val) \
    { \
        unsigned long pos = READ_ONCE(s->pos); \
        unsigned long sum = READ_ONCE(s->sum); \
        unsigned long sample = READ_ONCE(s->samples[pos]); \
      \
        BUILD_BUG_ON(!__builtin_constant_p(_samples));	\
      \
       WRITE_ONCE(s->sum, sum - sample + val); \
       WRITE_ONCE(s->samples[pos], val); \
       WRITE_ONCE(s->pos, pos + 1 == _samples ? 0 : pos + 1); \
    }

#endif /* __ETNAVIV_SMA_H__ */
