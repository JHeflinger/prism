#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "data/input.h"
#include "core/binds.h"

void LoadCow() {
    LoadOBJ("/home/jason/Dev/MESH/meshes/cow.obj");
    FitCamera();
}

void DevInitialize() {
    AddBind("load cow", LoadCow,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_C_OVERRIDE, BIND_KEY_PRESSED });
}

void DevUpdate() {
    if (InputKeyDown(IK_DEV)) {
        if (IsKeyDown(KEY_L)) {
            if (IsKeyPressed(KEY_C)) {
                //LoadOBJ("/home/jason/Dev/MESH/meshes/cow.obj");
                //FitCamera();
            } else if (IsKeyPressed(KEY_B)) {
                LoadXML("/home/jason/Dev/ADVGRAPHICS/example-scenes/CornellBox-Sphere.xml");
                SimpleCamera c = GetCamera();
                c.fov = 90.0f;
                MoveCamera(c);
            } else if (IsKeyPressed(KEY_O)) {
                LoadOBJ("/home/jason/Dev/MESH/meshes/icosahedron.obj");
                FitCamera();
            }
        } else if (IsKeyPressed(KEY_S)) {
            SaveRender("out.png");
        } else if (IsKeyPressed(KEY_C)) {
            ClearTriangles();
            ClearVertices();
            ClearNormals();
        }
    }
}

#else

void DevInitialize() {}

void DevUpdate() {}

#endif
