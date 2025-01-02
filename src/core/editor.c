#include "editor.h"
#include "data/config.h"
#include "core/log.h"
#include "raylib.h"
#include "easymemory.h"

void InitEditor() {
	SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(EDITOR_DEFAULT_WIDTH, EDITOR_DEFAULT_HEIGHT, "Prism");
}

void UpdateEditor() {

}

void DrawEditor() {
    ClearBackground(RAYWHITE);
    DrawText("hello world", 10, 10, 20, DARKGRAY);
}

void CleanEditor() {
    CloseWindow();
}

void RunEditor() {
    // Record memory status for clean check
    #ifndef PROD_BUILD
    size_t memcheck = EZALLOCATED();
    #endif

    // Initialize editor
    InitEditor();

    // Run editor
    while(!WindowShouldClose()) {

        // update editor
        UpdateEditor();

        // draw editor
        BeginDrawing();
        DrawEditor();
        EndDrawing();
    }

    // Close game
    CleanEditor();

    // Clean memory check
    LOG_ASSERT(memcheck == EZALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZALLOCATED() - memcheck));
}
