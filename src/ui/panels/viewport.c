#include "viewport.h"
#include "renderer/renderer.h"
#include "core/log.h"

TextureID tid;
Model model;
Mesh mesh;
int i = 0;

void DrawViewportPanel(float width, float height) {
    static float time = 0.0f;
    time += GetFrameTime() / 20.0f;
    SimpleCamera camera = GetCamera();
    camera.position.x = 3.0f * cos(time);
    camera.position.y = 3.0f * sin(time);
	camera.fov = 90.0f;
    MoveCamera(camera);
    Render();
    Draw(0, 0, width, height);
}

void ConfigureViewportPanel(Panel* panel) {
    SetupPanel(panel, "Viewport");
    panel->draw = DrawViewportPanel;

    TextureID tid = SubmitTexture("assets/images/room.png");

    Model model = LoadModel("assets/models/room.obj");
    LOG_ASSERT(model.meshCount != 0, "Failed to load model!");
    Mesh mesh = model.meshes[0];
    for (int i = 0; i < mesh.vertexCount / 3; i++) {
        Triangle triangle = { 0 };
        for (int j = 0; j < 3; j++) {
            Vertex vertex = {
                {
                    mesh.vertices[(i * 3 + j) * 3 + 0],
                    mesh.vertices[(i * 3 + j) * 3 + 1],
                    mesh.vertices[(i * 3 + j) * 3 + 2]
                },
                { 1.0f, 1.0f, 1.0f },
                {
                    mesh.texcoords[(i * 3 + j) * 2 + 0],
                    mesh.texcoords[(i * 3 + j) * 2 + 1]
                },
                tid
            };
            triangle.vertices[j] = vertex;
        }
        SubmitTriangle(triangle);
    }
    UnloadModel(model);
}
