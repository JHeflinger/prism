#ifndef LOG_H
#define LOG_H

#ifndef PROD_BUILD

#include <stdio.h>
#include <stdlib.h>

#define LOG_RESET "\033[0m"
#define LOG_RED "\033[31m"
#define LOG_BLUE "\033[34m"
#define LOG_GREEN "\033[32m"
#define LOG_YELLOW "\033[33m"
#define LOG_PURPLE "\033[35m"
#define LOG_CYAN "\033[36m"

#define LOG_INFO(...)  {printf("%s[INFO]%s  ", LOG_GREEN, LOG_RESET);  printf(__VA_ARGS__); printf("\n");}
#define LOG_FATAL(...) {printf("%s[FATAL]%s ", LOG_RED, LOG_RESET);    printf(__VA_ARGS__); printf("\n"); exit(1);}
#define LOG_WARN(...)  {printf("%s[WARN]%s  ", LOG_YELLOW, LOG_RESET); printf(__VA_ARGS__); printf("\n");}
#define LOG_DEBUG(...) {printf("%s[DEBUG]%s ", LOG_BLUE, LOG_RESET);   printf(__VA_ARGS__); printf("\n");}
#define LOG_CUSTOM(precursor, ...) {printf("%s[%s]%s  ", LOG_CYAN, precursor, LOG_RESET);   printf(__VA_ARGS__); printf("\n");}
#define LOG_SCAN(...)  {printf("%s[INPUT]%s ", LOG_PURPLE, LOG_RESET); scanf(__VA_ARGS__);}
#define LOG_ASSERT(x, ...) if (!(x)) { printf("%s[FAIL]%s  Assertion failed in %s:%d - \"", LOG_RED, LOG_RESET, __FILE__, __LINE__); printf(__VA_ARGS__); printf("\"\n"); exit(0); }

#else

#define LOG_INFO(...) ((void) 0)
#define LOG_FATAL(...) ((void) 0)
#define LOG_WARN(...) ((void) 0)
#define LOG_DEBUG(...) ((void) 0)
#define LOG_CUSTOM(precursor, ...) ((void) 0)
#define LOG_SCAN(...) ((void) 0)
#define LOG_ASSERT(x, ...) ((void) 0)

#endif

#endif