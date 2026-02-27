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
            Geometry* geometry = RendererGeometry();
            //SerialSubdivide(&(geometry->manifold));
            SerialSimplify(&(geometry->manifold), 1); //5204
            //EdgeCollapse(&(geometry->manifold), 0);
            if (TRUE || IsManifoldValid(&(geometry->manifold))) {
                SaveManifoldOBJ("out.obj", &(geometry->manifold));
                EZ_INFO("Saved processor output!");
                CleanManifoldMesh(&(geometry->manifold));
                ClearTriangles();
                ClearNormals();
                ClearVertices();
                LoadOBJ("out.obj");
            } else {
                EZ_ERROR("Manifold output was not valid");
            }
        } else if (IsKeyPressed(KEY_C)) {
            LoadOBJ("out.obj");
            SimpleCamera c = GetCamera();
            c.fov = 90.0f;
            MoveCamera(c);
        }
    }
}

#else

void DevUpdate() {}

#endif
