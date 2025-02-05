#include "viewport.h"
#include "renderer/renderer.h"
#include "core/log.h"

Model model;
Mesh mesh;
int i = 0;

void DrawViewportPanel(float width, float height) {
    static float time = 0.0f;
    static float radius = 3.0f;
    time += GetFrameTime();
    SimpleCamera camera = GetCamera();
    camera.position.x = radius * cos(time);
    camera.position.y = radius * sin(time);
	camera.fov = 90.0f;
    MoveCamera(camera);
    Render();
    Draw(0, 0, width, height);
    radius -= GetMouseWheelMove() / 4.0f;
}

void ConfigureViewportPanel(Panel* panel) {
    SetupPanel(panel, "Viewport");
    panel->draw = DrawViewportPanel;

    Model model = LoadModel("assets/models/room.obj");
    LOG_ASSERT(model.meshCount != 0, "Failed to load model!");
    Mesh mesh = model.meshes[0];
    for (int i = 0; i < mesh.vertexCount / 3; i++) {
        Triangle triangle = {
            {
                mesh.vertices[(i * 3 + 0) * 3 + 0],
                mesh.vertices[(i * 3 + 0) * 3 + 1],
                mesh.vertices[(i * 3 + 0) * 3 + 2]
            },
            {
                mesh.vertices[(i * 3 + 1) * 3 + 0],
                mesh.vertices[(i * 3 + 1) * 3 + 1],
                mesh.vertices[(i * 3 + 1) * 3 + 2]
            },
            {
                mesh.vertices[(i * 3 + 2) * 3 + 0],
                mesh.vertices[(i * 3 + 2) * 3 + 1],
                mesh.vertices[(i * 3 + 2) * 3 + 2]
            }
        };
        SubmitTriangle(triangle);
    }
    UnloadModel(model);
}
