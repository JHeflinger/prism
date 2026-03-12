#include "graph.h"
#include "renderer/renderer.h"
#include "data/colors.h"

#define GRAPH_FIDELITY 1.0f
#define DEFAULT_GRAPH_HEIGHT 30.0f

IMPL_ARRLIST(DataHistory);

ARRLIST_DataHistory g_histories = { 0 };
float g_yscale = 0.0f;
float g_currscale = DEFAULT_GRAPH_HEIGHT;
float g_ticks = 0.0f;

float GPUMemory100Mb() {
    size_t numheaps = GPUHeapCount();
    size_t allocated = 0;
    for (size_t i = 0; i < numheaps; i++) allocated += GPUHeapUsage(i);
    return (float)allocated / 100000000.0f;
}

float CPUMemoryMB() {
    return (float)EZ_ALLOCATED() / 1000000.0f;
}

float FrameTimeMilli() {
    return 1000.0f * GetFrameTime();
}

void AddHistory(const char* name, Color color, DataFunc updater) {
    ARRLIST_DataHistory_add(&g_histories, (DataHistory) {
        name, { 0 }, DEFAULT_GRAPH_HEIGHT, 0.0f, 0, 0, HISTORY_SIZE - 1, color, updater, TRUE
    });
}

void InitializeGraphPanel() {
    AddHistory("Render time (ms)", RED, RenderTime);
    AddHistory("Application time (ms)", BLUE, FrameTimeMilli);
    AddHistory("GPU Memory (100 MB)", GREEN, GPUMemory100Mb);
    AddHistory("CPU Memory (MB)", YELLOW, CPUMemoryMB);
}

void UpdateGraphPanel(float width, float height) {
    if (g_histories.size == 0) InitializeGraphPanel();
    g_yscale = 0.0f;
    g_ticks += 1.0f;
    for (size_t i = 0; i < g_histories.size; i++) {
        float val = g_histories.data[i].update();
        if (g_histories.data[i].enabled && g_histories.data[i].max > g_yscale) g_yscale = g_histories.data[i].max;
        if (g_histories.data[i].polled >= HISTORY_SIZE) {
            g_histories.data[i].polled = 0;
            g_histories.data[i].max = g_histories.data[i].inter;
            g_histories.data[i].inter = 0.0f;
        }
        g_histories.data[i].polled++;
        if (val > g_histories.data[i].inter) g_histories.data[i].inter = val;
        g_histories.data[i].data[g_histories.data[i].end] = val;
        g_histories.data[i].end = g_histories.data[i].end < HISTORY_SIZE - 1 ? g_histories.data[i].end + 1 : 0;
        g_histories.data[i].start = g_histories.data[i].start < HISTORY_SIZE - 1 ? g_histories.data[i].start + 1 : 0;
    }
    if (g_currscale != g_yscale) {
        float min, max, factor;
        if (g_currscale > g_yscale) {
            min = g_yscale;
            max = g_currscale;
            factor = 1.1f;
        } else {
            min = g_currscale;
            max = g_yscale;
            factor = 10.0f;
        }
        g_currscale = (max - min) / factor + min;
    }
    if (g_currscale > g_yscale - 0.1f && g_currscale < g_yscale + 0.1f) g_currscale = g_yscale;
}

void DrawGraphPanel(float width, float height) {
    float gwidth = width - 20;
    float gheight = gwidth * 0.75f;
    float stepsize = (float)HISTORY_SIZE / (gwidth / GRAPH_FIDELITY);
    float steps = HISTORY_SIZE / GRAPH_FIDELITY;
    float stepwidth = gwidth / steps;
    Vector2 gorigin = (Vector2){ 10, gheight + 10 };
    DrawRectangle(10, 10, gwidth, gheight, MappedColor(PANEL_GRAPH_BG_COLOR));
    DrawLine(gorigin.x, 10, gorigin.x, gorigin.y, MappedColor(PANEL_GRAPH_LINE_COLOR));
    DrawLine(gorigin.x, gorigin.y, gwidth + gorigin.x, gorigin.y, MappedColor(PANEL_GRAPH_LINE_COLOR));
    UISetCursor(gorigin.x, gorigin.y);
    UIDrawText("%.2f kt", (g_ticks - (float)HISTORY_SIZE) / 1000.0f);
    UISetCursor(gorigin.x + gwidth - UITextWidth("%.2f kt", g_ticks / 1000.0f), gorigin.y);
    UIDrawText("%.2f kt", g_ticks / 1000.0f);
    for (size_t i = 0; i < g_histories.size; i++) {
        if (!g_histories.data[i].enabled) continue;
        float currstep = g_histories.data[i].start;
        size_t step = 0;
        Vector2 prev = gorigin;
        for (size_t j = 0; j < (size_t)(HISTORY_SIZE / GRAPH_FIDELITY) + 1; j++) {
            size_t index = currstep;
            Vector2 point = (Vector2){ gorigin.x + step * stepwidth, 10 + gheight * (1.0f - (g_histories.data[i].data[index] / g_currscale)) };
            if (point.y > gheight + 10) point.y = gheight + 10;
            if (point.y < 10) point.y = 10;
            if (step != 0) DrawLine(prev.x, prev.y, point.x, point.y, g_histories.data[i].color);
            prev = point;
            step++;
            currstep += stepsize;
            if (currstep > (float)HISTORY_SIZE) currstep -= (float)HISTORY_SIZE;
        }
    }
    for (size_t i = 0; i < g_histories.size; i++) {
        float ylevel = 10 + gheight * (1.0f - (g_histories.data[i].max / g_currscale));
        if (ylevel < 10) ylevel = 10;
        if (ylevel > gheight + 10) ylevel = gheight + 10;
        float theight = UITextHeight("%.3f", g_histories.data[i].max);
        float tlevel = ylevel - theight / 2.0f;
        if (tlevel < 10) tlevel = 10;
        if (tlevel > gorigin.y - theight) tlevel = gorigin.y - theight;
        float llevel = gorigin.y + (i * 25) + 30;
        if (g_histories.data[i].enabled) {
            DrawLine(gorigin.x, ylevel, gorigin.x + gwidth, ylevel, MappedColor(PANEL_GRAPH_SUBTLE_LINE_COLOR));
            DrawLine(gorigin.x, ylevel, gorigin.x + 5, ylevel, MappedColor(PANEL_GRAPH_LINE_COLOR));
            UISetCursor(gorigin.x + 10, tlevel);
            UIDrawText("%.2f", g_histories.data[i].max);
        }
        UISetCursor(gorigin.x + 25, llevel);
        DrawRectangle(gorigin.x, llevel, 15, 15, g_histories.data[i].color);
        UIDrawText("%s", g_histories.data[i].name);
        UISetCursor(gwidth - 15, llevel);
        UICheckbox(&(g_histories.data[i].enabled));
    }
    // next:
    // 4. add toggle modes and key at bottom
}

void CleanGraphPanel() {
    ARRLIST_DataHistory_clear(&g_histories);
}

Panel GenerateGraphPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Profiling");
	p.draw = DrawGraphPanel;
    p.update = UpdateGraphPanel;
    p.clean = CleanGraphPanel;
	return p;
}
