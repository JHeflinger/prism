#include "viewport.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "renderer/loader.h"
#include "ui/panels/edit.h"
#include "data/input.h"
#include "core/binds.h"
#include <easylogger.h>
#include <rlgl.h>
#include <math.h>

RenderTexture2D g_viewport_target;
BOOL g_show_hints = FALSE;
BOOL g_rfocused = FALSE;
BOOL g_lfocused = FALSE;
BOOL g_zfocused = FALSE;
vec2 g_mousepoint = { 0 };

void ResetViewportCamera() {
    SimpleCamera camera = GetCamera();
    SETVEC3(camera.position, 0.0f, 2.133f, 2.11f);
    SETVEC3(camera.look, 0.0f, 0.0f, 0.0f);
    SETVEC3(camera.up, 0.0f, 1.0f, 0.0f);
    camera.fov = 90.0f;
    camera.aperature = 0.0f;
    camera.focus = 0.0f;
    MoveCamera(camera);
}

void ToggleHints() {
    g_show_hints = !g_show_hints;
}

void RotateCameraControls() {
    if (g_rfocused) {
        SimpleCamera camera = GetCamera();
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        if (radius < 1e-6f) radius = 1e-6f;
        float phi = acosf(offset[1] / radius) - (GetMouseDelta().y / 225.0);
        float theta = atan2f(offset[2], offset[0]) + (GetMouseDelta().x / 400.0f);
        if (phi < 0.001f) phi = 0.001f;
        if (phi > M_PI - 0.001f) phi = M_PI - 0.001f;
        camera.position[0] = camera.look[0] + (radius * sin(phi) * cos(theta));
        camera.position[1] = camera.look[1] + (radius * cos(phi));
        camera.position[2] = camera.look[2] + (radius * sin(phi) * sin(theta));
        MoveCamera(camera);
    }
}

void ZoomCameraControls() {
    if (g_zfocused) {
        vec2 mousedelta = { GetMouseDelta().x, GetMouseDelta().y };
        vec2 prev = { GetMousePosition().x, GetMousePosition().y };
        vec2 current = { GetMousePosition().x, GetMousePosition().y };
        glm_vec2_sub(prev, mousedelta, prev);
        vec2 o2c, o2p;
        glm_vec2_sub(g_mousepoint, prev, o2p);
        glm_vec2_sub(g_mousepoint, current, o2c);
        float to_previous = glm_vec2_norm(o2p);
        float to_current = glm_vec2_norm(o2c);
        float mousedelta_offset = glm_vec2_norm(mousedelta);
        SimpleCamera camera = GetCamera();
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        float rbefore = radius;
        radius -= (mousedelta_offset / 32.0f) * (to_previous < to_current ? -1.0f : 1.0f);
        if (radius < 1e-6f) radius = 1e-6f;
        glm_vec3_normalize(offset);
        glm_vec3_scale(offset, radius, offset);
        glm_vec3_add(camera.look, offset, camera.position);
        if (rbefore != radius) MoveCamera(camera);
    }
}

void PanCameraControls() {
    if (g_lfocused) {
        SimpleCamera camera = GetCamera();
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        if (radius < 1e-6f) radius = 1e-6f;
        float phi = acosf(offset[1] / radius);
        float theta = atan2f(offset[2], offset[0]);
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
        camera.position[0] = camera.look[0] + (radius * sin(phi) * cos(theta));
        camera.position[1] = camera.look[1] + (radius * cos(phi));
        camera.position[2] = camera.look[2] + (radius * sin(phi) * sin(theta));
        MoveCamera(camera);
    }
}

void DrawViewportPanel(float width, float height) {
    DrawTexturePro(
        g_viewport_target.texture,
        (Rectangle){ 0, 0, g_viewport_target.texture.width, -g_viewport_target.texture.height },
        (Rectangle){ 0, 0, g_viewport_target.texture.width, g_viewport_target.texture.height },
        (Vector2){ 0, 0 }, 0, WHITE);
    if (g_show_hints && HoveredPanel() && strcmp(HoveredPanel(), "Viewport") == 0) {
        DrawCurrentBinds(0, 0);
    }
}

void UpdateViewportPanel(float width, float height) {
    const char* hpanel = HoveredPanel();
    BOOL hovered = hpanel && strcmp(hpanel, "Viewport") == 0;
    if (InputButtonReleased(IK_MOUSERIGHT)) g_rfocused = FALSE;
    if (InputButtonReleased(IK_MOUSELEFT)) g_lfocused = FALSE;
    if (InputKeyReleased(IK_ZOOM)) {
        g_zfocused = FALSE;
        g_mousepoint[0] = GetMouseDelta().x;
        g_mousepoint[1] = GetMouseDelta().y;
    }
    if (InputButtonPressed(IK_MOUSERIGHT) && hovered) g_rfocused = TRUE;
    if (InputButtonPressed(IK_MOUSELEFT) && hovered) g_lfocused = TRUE;
    if (InputKeyPressed(IK_ZOOM)) g_zfocused = TRUE;
    SetViewportSlice(width, height);

    // camera scroll zoom
    {
        SimpleCamera camera = GetCamera();
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        float rbefore = radius;
        if (hovered) radius -= GetMouseWheelMove() * (0.05f * radius);
        if (radius < 1e-6f) radius = 1e-6f;
        glm_vec3_normalize(offset);
        glm_vec3_scale(offset, radius, offset);
        glm_vec3_add(camera.look, offset, camera.position);
        if (rbefore != radius) MoveCamera(camera);
    }

    // selection controls
    if (InputButtonPressed(IK_MOUSELEFT) && g_lfocused) {
        size_t tindex = HoveredTriangleIndex();
        if (tindex != (size_t)-1) {
            SetEditTriangle(tindex);
            SetSelectedTriangle(tindex);
        } else {
            DeselectEditTarget();
        }
    }

    // render
    Render();
    BeginTextureMode(g_viewport_target);
    Draw(0, 0, width, height);
    EndTextureMode();
}

Panel GenerateViewportPanel() {
	Panel p = { 0 };
    SetupPanel(&p, "Viewport");
	p.flush = TRUE;
    p.draw = DrawViewportPanel;
    p.update = UpdateViewportPanel;
    g_viewport_target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    AddBind("rotate viewport camera", RotateCameraControls, (BindCommand){ IK_MOUSERIGHT, BIND_BUTTON_END });
    AddBind("pan viewport camera", PanCameraControls, (BindCommand){ IK_MOUSELEFT, BIND_BUTTON_END });
    AddBind("zoom viewport camera", ZoomCameraControls, (BindCommand){ IK_ZOOM, BIND_KEY_END });
	AddBind("reset viewport camera", ResetViewportCamera, (BindCommand){ IK_RESET_CAMERA, BIND_KEY_PRESSED });
	AddBind("fit viewport camera to model", FitCamera, (BindCommand){ IK_FIT_CAMERA, BIND_KEY_PRESSED });
    AddBind("toggle input hints", ToggleHints, (BindCommand){ IK_TOGGLE_HINTS, BIND_KEY_PRESSED });
    return p;
}
