#include "viewport.h"
#include "renderer/renderer.h"
#include "core/log.h"

Model model;
Mesh mesh;
int i = 0;

void DrawViewportPanel(float width, float height) {
    static float time = 0.0f;
    static float time2 = 2.0f;
    static float radius = 3.0f;
    SimpleCamera camera = GetCamera();
    if (IsKeyDown(KEY_R)) {
        time += GetFrameTime();
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        time -= GetMouseDelta().x / 500.0;
        time2 += GetMouseDelta().y / 250.0;
    }
    camera.position.x = radius * cos(time);
    camera.position.y = radius * sin(time);
    camera.position.z = time2;
	camera.fov = 90.0f;
    SetViewportSlice(width, height);
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
    SurfaceMaterial material = {
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f },
        0.2f,
        0,
        0,
        0,
        0.0f,
        0
    };

    MaterialID mid = SubmitMaterial(material);

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
            },
            mid
        };
        SubmitTriangle(triangle);
    }
    UnloadModel(model);

    // submit some sdfs
    SubmitSDF((SDFPrimitive){SDF_SPHERE, {0, 0}});
}
