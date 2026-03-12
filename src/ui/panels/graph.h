#ifndef GRAPH_H
#define GRAPH_H

#include "ui/ui.h"

#define HISTORY_SIZE 300

typedef float (*DataFunc)(void);

typedef struct {
    const char* name;
    float data[HISTORY_SIZE];
    float max;
    float inter;
    size_t polled;
    size_t start;
    size_t end;
    Color color;
    DataFunc update;
    BOOL enabled;
} DataHistory;

DECLARE_ARRLIST(DataHistory);

Panel GenerateGraphPanel();

#endif
