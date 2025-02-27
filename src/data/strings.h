#ifndef STRINGS_H
#define STRINGS_H

#include <easyobjects.h>

typedef char* DynamicString;
typedef const char* StaticString;
DECLARE_ARRLIST(StaticString);
DECLARE_ARRLIST(DynamicString);

#endif