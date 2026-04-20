#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "data/input.h"
#include "core/binds.h"
#include "ui/panels/edit.h"

void LoadArmadillo() {
    LoadOBJ("assets/models/OBJ/naked/diamond.obj");
    FitCamera();
}

void LoadBox() {
    LoadFBX("assets/models/FBX/dragon.fbx");
    FitCamera();
}

void LoadPeter() {
    LoadOBJ("assets/models/OBJ/naked/peter.obj");
    SubmitNamedLight((SceneLight){{0},{1,1,1},{0,-1,0},0,0}, "Peter Overhead Light");
    FitCamera();
}

void Screenshot() {
    SaveRender("out.png");
}

void ClearScene() {
    ClearTriangles();
    ClearVertices();
    ClearNormals();
    ClearMeshDescriptors();
    ClearAnimations();
    DeselectEditTarget();
}

void DevInitialize() {
    AddBind("load armadillo", LoadArmadillo,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_A_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("load cornell box", LoadBox,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_B_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("load peter", LoadPeter,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_P_OVERRIDE, BIND_KEY_PRESSED });
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
