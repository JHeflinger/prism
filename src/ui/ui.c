#include "ui.h"
#include "core/log.h"
#include "easymemory.h"

UI* GenerateUI() {
    UI* ui = EZALLOC(1, sizeof(UI));
    return ui;
}

void UpdateUI(UI* ui) {

}

void DrawUI(UI* ui) {

}

void DestroyUI(UI* ui) {
    if (ui->left != NULL) DestroyUI((UI*)ui->left);
    if (ui->right != NULL) DestroyUI((UI*)ui->right);
    EZFREE(ui);
}