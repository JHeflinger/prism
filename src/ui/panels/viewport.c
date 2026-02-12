#include "viewport.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "data/input.h"
#include "ui/panels/edit.h"
#include "renderer/loader.h"
#include <easylogger.h>
#include <rlgl.h>
#include <math.h>

RenderTexture2D g_viewport_target;

void DrawViewportPanel(float width, float height) {
    DrawTexturePro(
        g_viewport_target.texture,
        (Rectangle){ 0, 0, g_viewport_target.texture.width, -g_viewport_target.texture.height },
        (Rectangle){ 0, 0, g_viewport_target.texture.width, g_viewport_target.texture.height },
        (Vector2){ 0, 0 }, 0, WHITE);
}

void UpdateViewportPanel(float width, float height) {
    const char* hpanel = HoveredPanel();
    BOOL hovered = hpanel && strcmp(hpanel, "Viewport") == 0;
    BOOL moved = FALSE;
    static BOOL lfocused = FALSE;
    static BOOL rfocused = FALSE;

    // reset camera
    if (InputKeyPressed(IK_RESET_CAMERA)) {
        SimpleCamera camera = GetCamera();
        SETVEC3(camera.position, 2.11f, 0.0f, 2.133f);
        SETVEC3(camera.look, 0.0f, 0.0f, 0.0f);
        SETVEC3(camera.up, 0.0f, 0.0f, 1.0f);
        camera.fov = 90.0f;
        camera.aperature = 0.0f;
        camera.focus = 0.0f;
        MoveCamera(camera);
        moved = TRUE;
    }

    // camera controls
    {
        SimpleCamera camera = GetCamera();
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        if (radius < 1e-6f) radius = 1e-6f;
        float phi = acosf(offset[2] / radius);
        float theta = atan2f(offset[1], offset[0]);
        if (InputButtonReleased(IK_MOUSERIGHT)) rfocused = FALSE;
        if (InputButtonReleased(IK_MOUSELEFT)) lfocused = FALSE;
        if (InputButtonPressed(IK_MOUSERIGHT) && hovered) rfocused = TRUE;
        if (InputButtonPressed(IK_MOUSELEFT) && hovered) lfocused = TRUE;
        if (InputButtonDown(IK_MOUSERIGHT) && rfocused) {
            phi -= GetMouseDelta().y / 225.0;
            theta -= GetMouseDelta().x / 400.0f;
            moved = TRUE;
        }
        if (phi < 0.001f) phi = 0.001f;
        if (phi > M_PI - 0.001f) phi = M_PI - 0.001f;
        if (InputKeyDown(IK_PAN_CAMERA) && InputButtonDown(IK_MOUSELEFT) && lfocused) {
            float distance_correction = radius / 600.0f;
            vec2 mouse_delta = { GetMouseDelta().x * distance_correction, GetMouseDelta().y * distance_correction };
            vec3 forward;
            glm_vec3_sub(camera.look, camera.position, forward);
            glm_vec3_normalize(forward);
            vec3 right, up;
            glm_vec3_cross(camera.up, forward, right);
            glm_vec3_normalize(right);
            glm_vec3_cross(forward, right, up);
            glm_vec3_normalize(up);
            vec3 movement;
            glm_vec3_scale(right, mouse_delta[0], right);
            glm_vec3_scale(up, mouse_delta[1], up);
            glm_vec3_add(right, up, movement);
            glm_vec3_add(camera.look, movement, camera.look);
            moved = TRUE;
        }
        if (hovered) {
            radius -= GetMouseWheelMove() / 4.0f;
            if (GetMouseWheelMove() != 0) moved = TRUE;
        }
        camera.position[0] = camera.look[0] + (radius * sin(phi) * cos(theta));
        camera.position[1] = camera.look[1] + (radius * sin(phi) * sin(theta));
        camera.position[2] = camera.look[2] + (radius * cos(phi));
        camera.fov = 90.0f;
        SetViewportSlice(width, height);
        if (moved) MoveCamera(camera);
    }

    // selection controls
    {
        if (InputButtonPressed(IK_MOUSELEFT) && lfocused) {
            TriangleID tid = HoveredTriangle();
            if (tid != (TriangleID)-1) {
                SetEditTriangle(HoveredTriangleIndex(tid));
                SetSelectedTriangle(tid);
            } else {
                DeselectEditTarget();
            }
            moved = TRUE;
        }
    }

    Render();
    BeginTextureMode(g_viewport_target);
    Draw(0, 0, width, height);
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
