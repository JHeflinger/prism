#include <core/extender.h>
#include "dice.h"

void ExtendPanelCreation(UI* ui) {
    ARRLIST_Panel_add(&(((UI*)(((UI*)ui->right)->right))->panels), GenerateDicePanel());
}
