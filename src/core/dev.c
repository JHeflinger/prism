#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "data/input.h"
#include "core/binds.h"
#include "ui/panels/edit.h"

static void LoadArmadillo() {
    LoadOBJ("assets/models/OBJ/dressed/gems.obj");
    SubmitNamedLight((SceneLight){{0},{1,1,1},{0,-1,0},0,0}, "Gems Overhead Light");
    FitCamera();
}

static void LoadBox() {
    LoadOBJ("assets/models/OBJ/dressed/CornellBox-Sphere.obj");
    FitCamera();
}

static void LoadPeter() {
    LoadOBJ("assets/models/OBJ/naked/peter.obj");
    SubmitNamedLight((SceneLight){{0},{1,1,1},{0,-1,0},0,0}, "Peter Overhead Light");
    FitCamera();
}

static void Screenshot() {
    SaveRender("out.png");
}

static void ClearSoftScene() {
    ClearScene(FALSE);
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
    AddBind("clear scene", ClearSoftScene,
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
