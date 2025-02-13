#include "viewport.h"
#include "renderer/renderer.h"
#include "core/log.h"

Model model;
Mesh mesh;
int i = 0;

void DrawViewportPanel(float width, float height) {
    static float radius = 3.0f;
    static float theta = 0.0f;
    static float phi = 0.78f;
    SimpleCamera camera = GetCamera();
    if (IsKeyDown(KEY_R)) {
        theta += GetFrameTime();
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        phi -= GetMouseDelta().y / 225.0;
        theta -= GetMouseDelta().x / 400.0f;
        if (phi < 0.001f) phi = 0.001f;
        if (phi > M_PI - 0.001f) phi = M_PI - 0.001f;
    }
    camera.position.x = radius * sin(phi) * cos(theta);
    camera.position.y = radius * sin(phi) * sin(theta);
    camera.position.z = radius * cos(phi);
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
        1.0f,
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
    //SubmitSDF((SDFPrimitive){SDF_SPHERE, {-0.75, 0.75, -0.75}, 1.0f});
    //SubmitSDF((SDFPrimitive){SDF_SPHERE, {0.75, -0.75, 0.75}, 1.0f, {0.0, 0.0, 0.0}});
    //SubmitSDF((SDFPrimitive){SDF_SPHERE, {0, 0, 0}, 1.0f, {0.0, 0.0, 0.0}});
    SubmitSDF((SDFPrimitive){SDF_JULIA, {0.0, 0.0, 0.0}, 1.0f, {0.0, 0.0, 0.0}});
    //SubmitSDF((SDFPrimitive){SDF_MANDELBULB, {0.0, 0.0, 0.0}, 1.0f});
    SubmitSDF((SDFPrimitive){SDF_BOX, {0.0, 0.0, -1.2}, 1.0f, {2.0, 2.0, 1.0}});
}
