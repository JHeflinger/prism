#include "vconfig.h"

BOOL g_super_enable_vk_validation_layers = TRUE;

void SUPER_DISABLE_VALIDATION_LAYERS() {
    g_super_enable_vk_validation_layers = FALSE;
}

BOOL VALIDATION_LAYERS_SUPER_ENABLED() {
    return g_super_enable_vk_validation_layers;
}