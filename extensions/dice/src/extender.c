#include <core/extender.h>
#include "dice.h"

void ExtendPanelCreation(UI* ui) {
    ARRLIST_Panel_add(&(((UI*)(((UI*)ui->right)->right))->panels), GenerateDicePanel());
}

void ExtendViewportUpdate(RenderTexture2D canvas, float width, float height) {
    DiceCanvasUpdate(canvas, width, height);
}
