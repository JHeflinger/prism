#include "assets.h"
#include <stddef.h>

Font g_font;

void InitializeAssets() {
    g_font = LoadFontEx("assets/fonts/OpenSans-Regular.ttf", MAX_FONT_SIZE, NULL, 0);
}

Font FontAsset() {
    return g_font;
}

void DestroyAssets() {
    UnloadFont(g_font);
}