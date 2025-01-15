#ifndef FILE_H
#define FILE_H

#include <stddef.h>

typedef struct {
	char* data;
	size_t size;
} SimpleFile;

SimpleFile* ReadFile(const char* filename);

void FreeFile(SimpleFile* file);

#endif
