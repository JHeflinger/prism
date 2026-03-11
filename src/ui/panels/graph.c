#include "graph.h"
#include "renderer/renderer.h"
#include "data/colors.h"

IMPL_ARRLIST(DataHistory);

ARRLIST_DataHistory g_histories = { 0 };
float g_yscale = 0.0f;
float g_currscale = -1.0f;

void AddHistory(const char* name, Color color, DataFunc updater) {
    ARRLIST_DataHistory_add(&g_histories, (DataHistory) { name, { 0 }, 0, HISTORY_SIZE - 1, color, updater });
}

void InitializeGraphPanel() {
    AddHistory("Render time (ms)", RED, RenderTime);
}

void UpdateGraphPanel(float width, float height) {
    if (g_histories.size == 0) InitializeGraphPanel();
    for (size_t i = 0; i < g_histories.size; i++) {
        float val = g_histories.data[i].update();
        if (val > g_yscale) g_yscale = val;
        g_histories.data[i].data[g_histories.data[i].end] = val;
        g_histories.data[i].end = g_histories.data[i].end < HISTORY_SIZE - 1 ? g_histories.data[i].end + 1 : 0;
        g_histories.data[i].start = g_histories.data[i].start < HISTORY_SIZE - 1 ? g_histories.data[i].start + 1 : 0;
    }
    if (g_currscale < 0) g_currscale = g_yscale;
}

void DrawGraphPanel(float width, float height) {
    if (g_currscale != g_yscale) {
        float max = g_currscale > g_yscale ? g_currscale : g_yscale;
        float min = g_currscale > g_yscale ? g_yscale : g_currscale;
        g_currscale = (max - min) / 2.0f + min;
    }
    UIDrawText("Test test %.3f", g_currscale);
    float gwidth = width - 20;
    Vector2 gorigin = (Vector2){ 10, gwidth * 0.75f + 10 };
    DrawRectangle(10, 10, gwidth, gwidth * 0.75f, MappedColor(PANEL_GRAPH_BG_COLOR));
    DrawLine(gorigin.x, 10, gorigin.x, gorigin.y, MappedColor(PANEL_GRAPH_LINE_COLOR));
    DrawLine(gorigin.x, gorigin.y, gwidth + gorigin.x, gorigin.y, MappedColor(PANEL_GRAPH_LINE_COLOR));
    UISetCursor(gorigin.x, gorigin.y);
    UIDrawText("0 ms");
    UISetCursor(gorigin.x + gwidth - UITextWidth("%.2f ms", (float)HISTORY_SIZE), gorigin.y);
    UIDrawText("%.2f ms", (float)HISTORY_SIZE);
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
