#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "data/input.h"

void DevUpdate() {
    if (InputKeyDown(IK_DEV)) {
        if (IsKeyPressed(KEY_L)) {
            LoadXML("/home/jason/Dev/ADVGRAPHICS/example-scenes/CornellBox-Sphere.xml");
            SimpleCamera c = GetCamera();
            c.fov = 90.0f;
            MoveCamera(c);
        } else if (IsKeyPressed(KEY_S)) {
            SaveRender("out.png");
        }
    }
}

#else

void DevUpdate() {}

#endif
