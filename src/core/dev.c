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
            SimpleCamera c = GetCamera();
            c.fov = 90.0f;
            glm_vec3_scale(c.position, 10.0f, c.position);
            MoveCamera(c);
        } else if (IsKeyPressed(KEY_S)) {
            SaveRender("out.png");
        } else if (IsKeyPressed(KEY_O)) {
            Subdivide();
        } else if (IsKeyPressed(KEY_C)) {
            Simplify(5204);
        }
    }
}

#else

void DevUpdate() {}

#endif
