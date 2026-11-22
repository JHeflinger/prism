#include "draw.h"
#include "renderer/overlay.h"
#include "ui/panels/viewport.h"
#include "ui/panels/edit.h"
#include "game/definitions.h"
#include "game/systems/system.h"
#include "game/ecs/components.h"
#include "game/ecs/entity.h"
#include "game/world.h"
#include "game/scene.h"
#include <core/config.h>
#include <data/colors.h>
#include <util/logger.h>
#include <easysort.h>
#include <raymath.h>
#include <float.h>

DECLARE_EASYSORT(EntityID);
IMPL_EASYSORT(EntityID);

typedef void (*DrawComponentFunc)(Entity, Vector2);

static GameWorld* g_current_world = NULL;

static inline float ExtractZValue(EntityID e) {
    Entity entity = (Entity){ e, g_current_world };
    return GetWorldPosition(entity).z; 
}

static void DrawImageComponent(Entity e, Vector2 origin) {
    ImageComponent* tc = GetComponent(e, ImageComponent);
    if (tc->id != (size_t)-1) {
        EZ_ASSERT(tc->id < e.context->parent->assets.images.size, "Invalid Texture ID [%d] detected", (int)tc->id);
        Texture2D tex = e.context->parent->assets.images.data[tc->id];
        Vector3 translation = GetWorldPosition(e);
        Vector3 scale = GetWorldScale(e);
        DrawTexturePro(
            tex,
            (Rectangle){0, 0, tex.width, tex.height},
            (Rectangle){
                translation.x - scale.x/2.0f + origin.x,
                translation.y - scale.y/2.0f + origin.y,
                scale.x, scale.y},
            (Vector2){0, 0}, GetWorldRotation(e).z, tc->tint);
    }
}

static void DrawTextComponent(Entity e, Vector2 origin) {
    TextComponent* tc = GetComponent(e, TextComponent);
    Font default_font = GetFontDefault();
    Vector2 text_size = MeasureTextEx(default_font, tc->text, tc->size * GetWorldScale(e).x, 2);
    Vector3 translation = GetWorldPosition(e);
    Vector2 tpos = (Vector2){ translation.x + origin.x, translation.y + origin.y };
    tpos.y -= text_size.y / 2.0f;
    switch (tc->alignment) {
        case TEXT_ALIGN_CENTER:
            tpos.x -= text_size.x / 2.0f;
            break;
        case TEXT_ALIGN_LEFT: break;
        case TEXT_ALIGN_RIGHT:
            tpos.x -= text_size.x;
            break;
        default: break;
    }
    DrawTextEx(default_font, tc->text, tpos, tc->size * GetWorldScale(e).x, 2, tc->color);
}

static void DrawShapeComponent(Entity e, Vector2 origin) {
    ShapeComponent* sc = GetComponent(e, ShapeComponent);
    Vector3 translation = GetWorldPosition(e);
    Vector3 scale = GetWorldScale(e);
    if (sc->type == RECTANGLE_SHAPE) {
        DrawRectangle(translation.x - (scale.x / 2.0f) + origin.x,
                      translation.y - (scale.y / 2.0f) + origin.y,
                      scale.x, scale.y, sc->color);
    } else {
        float radius = (scale.x + scale.y) / 4.0f;
        DrawCircle(translation.x + origin.x, translation.y + origin.y, radius, sc->color);
    }
}

static Vector2 AnchorCoordinate(Entity e) {
    Rectangle vprec = GetViewportRec();
    Vector2 slice = (Vector2){ vprec.width, vprec.height };
    Vector2 tpos;
    switch (GetComponent(e, AnchorComponent)->anchor) {
        case CENTER_ANCHOR:
            tpos = (Vector2){ slice.x / 2.0f, slice.y / 2.0f };
            break;
        case ML_ANCHOR:
            tpos = (Vector2){ 0, slice.y / 2.0f };
            break;
        case MR_ANCHOR:
            tpos = (Vector2){ slice.x, slice.y / 2.0f };
            break;
        case TL_ANCHOR:
            tpos = (Vector2){ 0, 0 };
            break;
        case TR_ANCHOR:
            tpos = (Vector2){ slice.x, 0 };
            break;
        case BR_ANCHOR:
            tpos = slice;
            break;
        case BL_ANCHOR:
            tpos = (Vector2){ 0, slice.y };
            break;
        case TM_ANCHOR:
            tpos = (Vector2){ slice.x / 2.0f, 0 };
            break;
        case BM_ANCHOR:
            tpos = (Vector2){ slice.x / 2.0f, slice.y };
            break;
        default: break;
    }
    return tpos;
}

static void Draw2DElements(GameSystem* system) {
    g_current_world = system->context;
    ARRLIST_EntityID* eids = GetEntities(system->context, ImageComponent);
    if (eids) EasySort_EntityID(eids, ExtractZValue);
    eids = GetEntities(system->context, ShapeComponent);
    if (eids) EasySort_EntityID(eids, ExtractZValue);
    eids = GetEntities(system->context, TextComponent);
    if (eids) EasySort_EntityID(eids, ExtractZValue);
    ARRLIST_EntityID* images = GetEntities(system->context, ImageComponent);
    ARRLIST_EntityID* shapes = GetEntities(system->context, ShapeComponent);
    ARRLIST_EntityID* texts = GetEntities(system->context, TextComponent);
    ARRLIST_EntityID* cameras = GetEntities(system->context, Camera2DComponent);
    ARRLIST_EntityID anchors = { 0 };
    ARRLIST_int anchorfuncs = { 0 };
    #define DRAWTYPES 3
    ARRLIST_EntityID* lists[DRAWTYPES] = { images, shapes, texts };
    DrawComponentFunc funcs[DRAWTYPES] = { DrawImageComponent, DrawShapeComponent, DrawTextComponent };
    size_t indices[DRAWTYPES] = { 0 };
    Camera2D camera = { 0 };//Camera2D camera = GetViewportCamera(); TODO: redo this
    if (/*Playing() && */cameras) {// TODO: playinhg
        for (size_t i = 0; i < cameras->size; i++) {
            Entity e = (Entity){ cameras->data[i], system->context };
            Camera2DComponent* cc = GetComponent(e, Camera2DComponent);
            if (cc->enabled) {
                camera.target = Vector2Add((Vector2){ GetWorldPosition(e).x, GetWorldPosition(e).y }, cc->offset);
                camera.rotation = cc->rotation + GetWorldRotation(e).z;
                camera.zoom = cc->zoom;
                break;
            }
        }
    }
    BeginMode2D(camera);
    while (TRUE) {
        float zs[DRAWTYPES] = { FLT_MAX, FLT_MAX, FLT_MAX };
        BOOL done = TRUE;
        for (int i = 0; i < DRAWTYPES; i++) {
            if (lists[i] && indices[i] < lists[i]->size) {
                done = FALSE;
                zs[i] = ExtractZValue(lists[i]->data[indices[i]]);
            }
        }
        if (done) break;
        int min = 0;
        for (int i = 1; i < DRAWTYPES; i++) {
            if (zs[i] < zs[min]) {
                min = i;
            }
        }
        Entity e = (Entity){ lists[min]->data[indices[min]], system->context };
        if (HasComponent(e, AnchorComponent)) {
            ARRLIST_EntityID_add(&anchors, e.id);
            ARRLIST_int_add(&anchorfuncs, min);
        } else {
            funcs[min](e, (Vector2){ 0, 0 });
        }
        indices[min]++;
    }
    EndMode2D();
    for (size_t i = 0; i < anchors.size; i++) {
        Entity e = (Entity){ anchors.data[i], system->context };
        funcs[anchorfuncs.data[i]](e, AnchorCoordinate(e));
    }
    ARRLIST_EntityID_clear(&anchors);
    ARRLIST_int_clear(&anchorfuncs);
    #undef DRAWTYPES
}

static void DrawDrawSystem(GameSystem* system) {
    Draw2DElements(system);
}

GameSystem* GenerateDrawSystem() {
    return GenerateSystem(DrawDrawSystem, NULL, NULL, NULL, NULL, NULL, NULL);
}
