#include "viewport.h"
#include "renderer/renderer.h"

void DrawViewportPanel(float width, float height) {
    static float time = 0.0f;
    time += GetFrameTime();
    SimpleCamera camera = GetCamera();
    camera.position.x = 3.0f * cos(time);
    camera.position.y = 3.0f * sin(time);
    MoveCamera(camera);
    Render();
    Draw(0, 0, width, height);
}

void ConfigureViewportPanel(Panel* panel) {
    SetupPanel(panel, "Viewport");
    panel->draw = DrawViewportPanel;

    TextureID tid = SubmitTexture("assets/images/room.png");
}