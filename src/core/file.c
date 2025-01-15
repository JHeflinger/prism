#include "file.h"
#include "core/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <easymemory.h>

SimpleFile* ReadFile(const char* filename) {
	SimpleFile* sfile = EZALLOC(1, sizeof(SimpleFile));
	FILE* file = fopen(filename, "rb");
	LOG_ASSERT(file != NULL, "Error opening file");
	fseek(file, 0, SEEK_END);
	sfile->size = ftell(file);
	rewind(file);
	sfile->data = EZALLOC(sfile->size, sizeof(char));
	size_t read = fread(sfile->data, 1, sfile->size, file);
	LOG_ASSERT(read == sfile->size, "Error reading file");
	fclose(file);
	return sfile;
}

void FreeFile(SimpleFile* file) {
	EZFREE(file->data);
	EZFREE(file);
}
