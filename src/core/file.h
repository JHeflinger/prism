#ifndef FILE_H
#define FILE_H

#include <stddef.h>
#include <easybool.h>

typedef enum {
    UNKNOWN = 0,
    DOTPRISM,
    DOTOBJ,
    DOTSPV,
    DOTMTL
} FileType;

typedef struct {
	char* data;
	size_t size;
    FileType type;
} SimpleFile;

typedef struct {
    SimpleFile* file;
    size_t line;
    size_t cursor;
} LineParser;

char* StripFilename(char* path);

SimpleFile* ReadFile(const char* filename);

void FreeFile(SimpleFile* file);

LineParser Parser(SimpleFile* file);

BOOL NextLine(LineParser* lp, char* buffer, size_t size);

#endif
