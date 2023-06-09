#include "kt_common.h"

inline i64 kt_get_realtime_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

inline i64 kt_get_time_diff_ns(i64 start, i64 end)
{
    return end - start;
}
