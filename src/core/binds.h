#ifndef BINDS_H
#define BINDS_H

#include "data/input.h"
#include <easybasics.h>

typedef void (*BindFunc)(void);

typedef enum {
    BIND_BUTTON_PRESSED,
    BIND_BUTTON_RELEASED,
    BIND_BUTTON_DOWN,
    BIND_BUTTON_END,
    BIND_KEY_PRESSED,
    BIND_KEY_RELEASED,
    BIND_KEY_DOWN,
    BIND_KEY_END,
} BindAction;

typedef struct {
    uint32_t input;
    BindAction action;
} BindCommand;

typedef struct {
    BindFunc func;
    const char* name;
    BindCommand command;
    ARRLIST_voidPtr nodes;
} BindNode;

void AddBind(const char* name, BindFunc func, ...);

void ListenBinds();

void CleanBinds();

void DrawCurrentBinds(float x, float y);

#endif
