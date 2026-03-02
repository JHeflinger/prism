#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/processor.h"
#include "renderer/loader.h"
#include "data/input.h"

void DevUpdate() {
    if (InputKeyDown(IK_DEV)) {
        if (IsKeyPressed(KEY_L)) {
            LoadOBJ("/home/jason/Dev/MESH/meshes/cow.obj");
            //LoadXML("/home/jason/Dev/ADVGRAPHICS/example-scenes/CornellBox-Sphere.xml");
            SimpleCamera c = GetCamera();
            c.fov = 90.0f;
            MoveCamera(c);
            FitCamera();
        } else if (IsKeyPressed(KEY_S)) {
            SaveRender("out.png");
        }
    }
}

#else

void DevUpdate() {}

#endif
