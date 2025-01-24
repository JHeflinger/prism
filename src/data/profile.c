#include "profile.h"
#include "core/log.h"
#include <raylib.h>
#include <string.h>

float ProfileResult(Profiler* profiler) {
    return profiler->average;
}

void ConfigureProfile(Profiler* profiler, const char* name, size_t step) {
    if (step >= PROFILER_MAX_DATASTREAM) LOG_FATAL("Profiler step size too big");
    profiler->name = name;
    profiler->step = step;
}

void BeginProfile(Profiler* profiler) {
    profiler->curr = GetTime();
}

void EndProfile(Profiler* profiler) {
    profiler->curr = GetTime() - profiler->curr;
    profiler->curr = profiler->curr * 1000.0;
    uint64_t copy[PROFILER_MAX_DATASTREAM];
    memcpy(copy, profiler->datastream, PROFILER_MAX_DATASTREAM * sizeof(double));
    memcpy(profiler->datastream + 1, copy, (PROFILER_MAX_DATASTREAM - 1) * sizeof(double));
    profiler->datastream[0] = profiler->curr;
    profiler->average = 0.0f;
    for (size_t i = 0; i < profiler->step; i++)
        profiler->average += (float)profiler->datastream[i] / (float)profiler->step;
}