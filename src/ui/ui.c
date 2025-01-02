#include "ui.h"
#include "easymemory.h"

UI* GenerateUI() {
    UI* ui = EZALLOC(1, sizeof(UI));
    return ui;
}

BOOL InjectIntoUI(UI* ui, PanelPlacement placement) {
    return FALSE;
}

void UpdateUI(UI* ui) {

}

void DrawUI(UI* ui) {

}

void DestroyUI(UI* ui) {
    for (size_t i = 0; i < 9; i++)
        if (ui->internal[i] != NULL) DestroyUI((UI*)ui->internal[i]);
    EZFREE(ui);
}