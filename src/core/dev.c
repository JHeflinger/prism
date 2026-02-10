#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "data/input.h"

void DevUpdate() {
    if (InputKeyPressed(IK_DEV)) {
        LoadXML("/home/jason/Dev/ADVGRAPHICS/example-scenes/CornellBox-Glossy.xml");
        SimpleCamera c = GetCamera();
        c.fov = 90.0f;
        MoveCamera(c);
    }
}

#else

void DevUpdate() {}

#endif
