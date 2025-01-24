#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>
#include <stddef.h>

#define PROFILER_MAX_DATASTREAM 512

typedef struct {
    double datastream[PROFILER_MAX_DATASTREAM];
    float average;
    const char* name;
    size_t step;
    double curr;
} Profiler;

float ProfileResult(Profiler* profiler);

void ConfigureProfile(Profiler* profiler, const char* name, size_t step);

void BeginProfile(Profiler* profiler);

void EndProfile(Profiler* profiler);

#endif