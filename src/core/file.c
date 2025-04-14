#include "file.h"
#include <easylogger.h>
#include <stdio.h>
#include <stdlib.h>
#include <easymemory.h>

SimpleFile* ReadFile(const char* filename) {
	SimpleFile* sfile = EZ_ALLOC(1, sizeof(SimpleFile));
	FILE* file = fopen(filename, "rb");
	EZ_ASSERT(file != NULL, "Error opening file");
	fseek(file, 0, SEEK_END);
	sfile->size = ftell(file);
	rewind(file);
	sfile->data = EZ_ALLOC(sfile->size, sizeof(char));
	size_t read = fread(sfile->data, 1, sfile->size, file);
	EZ_ASSERT(read == sfile->size, "Error reading file");
	fclose(file);
	return sfile;
}

void FreeFile(SimpleFile* file) {
	EZ_FREE(file->data);
	EZ_FREE(file);
}
