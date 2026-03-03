#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "data/input.h"
#include "core/binds.h"
#include "ui/panels/edit.h"

void LoadCow() {
    LoadOBJ("/home/jason/Dev/MESH/meshes/cow.obj");
    FitCamera();
}

void LoadBox() {
    LoadXML("/home/jason/Dev/ADVGRAPHICS/example-scenes/CornellBox-Sphere.xml");
    SimpleCamera c = GetCamera();
    c.fov = 90.0f;
    MoveCamera(c);
}

void LoadDice() {
    LoadOBJ("/home/jason/Dev/MESH/meshes/icosahedron.obj");
    FitCamera();
}

void Screenshot() {
    SaveRender("out.png");
}

void ClearScene() {
    ClearTriangles();
    ClearVertices();
    ClearNormals();
    DeselectEditTarget();
}

void DevInitialize() {
    AddBind("load cow", LoadCow,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_C_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("load icosahedron", LoadDice,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_O_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("load cornell box", LoadBox,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_B_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("clear scene", ClearScene,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_C_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("screenshot", Screenshot,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_S_OVERRIDE, BIND_KEY_PRESSED });
}

void DevUpdate() {}

#else

void DevInitialize() {}

void DevUpdate() {}

#endif
