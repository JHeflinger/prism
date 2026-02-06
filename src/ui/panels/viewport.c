#include "viewport.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "data/input.h"
#include "ui/panels/edit.h"
#include "renderer/loader.h"
#include <easylogger.h>
#include <rlgl.h>

RenderTexture2D g_viewport_target;

void DrawViewportPanel(float width, float height) {
    DrawTexturePro(
        g_viewport_target.texture,
        (Rectangle){ 0, 0, g_viewport_target.texture.width, -g_viewport_target.texture.height },
        (Rectangle){ 0, 0, g_viewport_target.texture.width, g_viewport_target.texture.height },
        (Vector2){ 0, 0 }, 0, WHITE);
}

void UpdateViewportPanel(float width, float height) {
    // camera controls
    {
        static float radius = 3.0f;
        static float theta = 0.0f;
        static float phi = 0.78f;
        SimpleCamera camera = GetCamera();
        if (InputButtonDown(IK_MOUSERIGHT)) {
            phi -= GetMouseDelta().y / 225.0;
            theta -= GetMouseDelta().x / 400.0f;
            if (phi < 0.001f) phi = 0.001f;
            if (phi > M_PI - 0.001f) phi = M_PI - 0.001f;
        }
        if (InputKeyDown(IK_PAN_CAMERA) && InputButtonDown(IK_MOUSELEFT)) {
            float distance_correction = radius / 600.0f;
            vec3 lookat_pos = { camera.look.x, camera.look.y, camera.look.z };
            vec3 camera_pos = { camera.position.x, camera.position.y, camera.position.z };
            vec2 mouse_delta = { GetMouseDelta().x * distance_correction, GetMouseDelta().y * distance_correction };
            vec3 forward;
            glm_vec3_sub(lookat_pos, camera_pos, forward);
            glm_vec3_normalize(forward);
            vec3 right, up;
            vec3 camera_up = { camera.up.x, camera.up.y, camera.up.z };
            glm_vec3_cross(camera_up, forward, right);
            glm_vec3_normalize(right);
            glm_vec3_cross(forward, right, up);
            glm_vec3_normalize(up);
            vec3 movement;
            glm_vec3_scale(right, mouse_delta[0], right);
            glm_vec3_scale(up, mouse_delta[1], up);
            glm_vec3_add(right, up, movement);
            camera.look.x += movement[0];
            camera.look.y += movement[1];
            camera.look.z += movement[2];
        }
        if (InputKeyPressed(IK_RESET_CAMERA)) {
            //radius = 3.0f;
            //theta = 0.0f;
            //phi = 0.78f;
            radius = 2.750;
            theta = 4.715;
            phi = 0.001;
            //camera.look = (Vector3){ 0, 0, 0 };
            camera.look = (Vector3){ 0, 1, 0 };
            LoadOBJ("/home/jason/Dev/ADVGRAPHICS/example-scenes/models/CornellBox/CornellBox-Sphere.obj"); 
        }
        camera.position.x = camera.look.x + (radius * sin(phi) * cos(theta));
        camera.position.y = camera.look.y + (radius * sin(phi) * sin(theta));
        camera.position.z = camera.look.z + (radius * cos(phi));
        camera.fov = 90.0f;
        SetViewportSlice(width, height);
        MoveCamera(camera);
        radius -= GetMouseWheelMove() / 4.0f;
    }

    // selection controls
    {
        if (InputButtonReleased(IK_MOUSELEFT)) {
            TriangleID tid = HoveredTriangle();
            if (tid != (TriangleID)-1) {
                SetEditTriangle(HoveredTriangleIndex(tid));
                SetSelectedTriangle(tid);
            }
        }
    }

    Render();
    BeginTextureMode(g_viewport_target);
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE, RL_FUNC_ADD, RL_MAX);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    Draw(0, 0, width, height);
    EndBlendMode();
    EndTextureMode();

    // TEMPORARY
    if (IsKeyPressed(KEY_S)) {
        SaveRender("out.png");
    }
}

Panel GenerateViewportPanel() {
	Panel p = { 0 };
    SetupPanel(&p, "Viewport");
	p.flush = TRUE;
    p.draw = DrawViewportPanel;
    p.update = UpdateViewportPanel;
    g_viewport_target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
	return p;
}
