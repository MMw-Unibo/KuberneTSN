#ifndef KT_COMMON_H
#define KT_COMMON_H

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

#define MSEC_PER_SEC 1000LL
#define USEC_PER_SEC 1000000LL
#define NSEC_PER_SEC 1000000000LL

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// Memory Barriers
#define kt_comp_barrier()         \
    do                            \
    {                             \
        asm volatile(""           \
                     :            \
                     :            \
                     : "memory"); \
    } while (0)

#define kt_smp_rmb() kt_comp_barrier()

#define kt_pause()             \
    do                         \
    {                          \
        asm volatile("pause"); \
    } while (0)

#define _kt_packed __attribute__((packed))

#define _kt_aligned(a) __attribute__((__aligned__(a)))

#define KT_CACHE_LINE_MIN_SIZE 64

#define _kt_cache_aligned _kt_aligned(KT_CACHE_LINE_MIN_SIZE)

i64 kt_get_realtime_ns();

i64 kt_get_time_diff_ns(i64 start, i64 end);

#endif // KT_COMMON_H