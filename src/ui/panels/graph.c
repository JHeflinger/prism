#include "graph.h"
#include "renderer/renderer.h"

IMPL_ARRLIST(DataHistory);

ARRLIST_DataHistory g_histories = { 0 };

void AddHistory(const char* name, Color color, DataFunc updater) {
    ARRLIST_DataHistory_add(&g_histories, (DataHistory) { name, { 0 }, 0, HISTORY_SIZE - 1, color, updater });
}

void InitializeGraphPanel() {
    AddHistory("Render time (ms)", RED, RenderTime);
}

void UpdateGraphPanel(float width, float height) {
    if (g_histories.size == 0) InitializeGraphPanel();
    for (size_t i = 0; i < g_histories.size; i++) {
        g_histories.data[i].data[g_histories.data[i].end] = g_histories.data[i].update();
        g_histories.data[i].end = g_histories.data[i].end < HISTORY_SIZE - 1 ? g_histories.data[i].end + 1 : 0;
        g_histories.data[i].start = g_histories.data[i].start < HISTORY_SIZE - 1 ? g_histories.data[i].start + 1 : 0;
    }
}

void DrawGraphPanel(float width, float height) {
    UIDrawText("Test test");
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
