#include "edit.h"
#include "renderer/subsurface.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "renderer/loader.h"
#include "renderer/rmath.h"
#include "renderer/texture.h"
#include "renderer/animation.h"
#include "renderer/rig.h"
#include "ui/shared.h"
#include <data/input.h>
#include <util/logger.h>
#include <ui/extra.h>
#include <easyfile.h>

#define TriangleHasUVs(t) ((t).at != NO_UV && (t).bt != NO_UV && (t).ct != NO_UV)

static size_t g_edit_item_index = 0;
static BOOL g_item_selected = FALSE;
static EditType g_edit_type = EDIT_MATERIAL;
static const char* g_light_types[] = { "Directional", "Spot", "Point", "Environment" };
static uint32_t g_preview_level = 0;

typedef struct {
    SurfaceMaterial* material;
    size_t slot;
} MaterialSlotRef;

static MaterialSlotRef g_slot_refs[MATERIAL_TEXTURE_SLOTS];

static size_t ClampHandle(TextureHandle handle) {
    return handle <= NumTextures() ? (size_t)handle : 0;
}

static size_t DropdownSelectMaterialTexture(void* data, size_t index, BOOL cancel) {
    MaterialSlotRef* ref = (MaterialSlotRef*)data;
    if (index == (size_t)-1) return ClampHandle(ref->material->textures[ref->slot]);
    ref->material->textures[ref->slot] = (TextureHandle)index;
    if (ref->slot == TEXSLOT_EMISSION) UpdateEmissives();
    UpdateMaterials();
    ResetAccumulation();
    return index;
}

static size_t DropdownSelectLightTexture(void* data, size_t index, BOOL cancel) {
    SceneLight* light = (SceneLight*)data;
    if (index == (size_t)-1) return ClampHandle(light->texture);
    light->texture = (TextureHandle)index;
    UpdateLights();
    if (light->mode == LIGHT_MODE_IBL) UpdateTextures();
    ResetAccumulation();
    return index;
}

static size_t DropdownSelectLightMode(void* data, size_t index, BOOL cancel) {
    SceneLight* light = (SceneLight*)data;
    if (index == (size_t)-1) return light->mode < LIGHT_MODE_COUNT ? light->mode : 0;
    BOOL ibl = light->mode == LIGHT_MODE_IBL || index == LIGHT_MODE_IBL;
    light->mode = (uint32_t)index;
    UpdateLights();
    if (ibl && light->texture) UpdateTextures();
    ResetAccumulation();
    return index;
}

static size_t DropdownSelectObjectFilter(void* data, size_t index, BOOL cancel) {
    MeshDescriptor* md = (MeshDescriptor*)data;
    if (index == (size_t)-1) return md->filter < TEXFILTER_COUNT ? md->filter : 0;
    md->filter = (uint32_t)index;
    UpdateMeshes();
    ResetAccumulation();
    return index;
}

static size_t DropdownSelectMipFilter(void* data, size_t index, BOOL cancel) {
    SceneTexture* tex = (SceneTexture*)data;
    if (index == (size_t)-1) return tex->settings.mipfilter < MIPFILTER_COUNT ? tex->settings.mipfilter : 0;
    tex->settings.mipfilter = (uint32_t)index;
    return index;
}

static size_t DropdownSelectTextureWrap(void* data, size_t index, BOOL cancel) {
    SceneTexture* tex = (SceneTexture*)data;
    if (index == (size_t)-1) return tex->settings.wrap < TEXWRAP_COUNT ? tex->settings.wrap : 0;
    tex->settings.wrap = (uint32_t)index;
    UpdateTextures();
    return index;
}

static void ExportEditTarget() {
    if (!g_item_selected) { logwarn("Nothing selected to export"); return; }
    PBJType type;
    switch (g_edit_type) {
        case EDIT_CAMERA: type = PBJ_CAMERA; break;
        case EDIT_MATERIAL: type = PBJ_MATERIAL; break;
        case EDIT_LIGHT: type = PBJ_LIGHT; break;
        case EDIT_MESH: type = PBJ_MESH; break;
        case EDIT_SINGLE_FORCE: type = PBJ_FORCE; break;
        case EDIT_SINGLE_SOURCE: type = PBJ_SOURCE; break;
        case EDIT_TEXTURE: type = PBJ_TEXTURE; break;
        case EDIT_BONE: logerror("Bones are exported with their object - select the object and export it"); return;
        default: logerror("Single triangles and vertices can't be exported"); return;
    }
    const char* filepath = GetSaveFile("pbj");
    if (filepath) SavePBJ(type, g_edit_item_index, filepath);
}

static void DrawEditMaterial(SurfaceMaterial* matref, char* name, float width, float height) {
    BOOL edited = FALSE;
    BOOL was_emissive = matref->emission[0] != 0 || matref->emission[1] != 0 || matref->emission[2] != 0;
    UIMoveCursor((width - 20 - UITextWidth("Edit Material")) / 2.0f, 0);
    UIDrawText("Edit Material");
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_MATERIAL_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 15);
    UIDividerLabeled(width, "Properties");
    UIMoveCursor(0, 5);
    UIColumnHeader("Emission", LEFT_COLUMN_WIDTH);
    BOOL emission_edited = UITriplet("rgb", UI_FLOATS, &(matref->emission[0]), &(matref->emission[1]), &(matref->emission[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    edited |= emission_edited;
    BOOL is_emissive = matref->emission[0] != 0 || matref->emission[1] != 0 || matref->emission[2] != 0;
    UIColumnHeader("Absorbtion", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->absorbtion[0]), &(matref->absorbtion[1]), &(matref->absorbtion[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.001f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Dispersion", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->dispersion[0]), &(matref->dispersion[1]), &(matref->dispersion[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.001f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Ambient", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->ambient[0]), &(matref->ambient[1]), &(matref->ambient[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = 1.0f }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Diffuse", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->diffuse[0]), &(matref->diffuse[1]), &(matref->diffuse[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = 1.0f }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Specular", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->specular[0]), &(matref->specular[1]), &(matref->specular[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = 1.0f }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Scattering", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->scattering[0]), &(matref->scattering[1]), &(matref->scattering[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIMoveCursor(0, 35);
    float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
    UIColumnHeader("Index of Refraction", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->ior), 0, FLT_MAX, 0.01f, sboxwidth);
    UIColumnHeader("Shininess", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->shiny), 0, FLT_MAX, 0.01f, sboxwidth);
    UIColumnHeader("Anisotropy", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->anisotropy), 0, FLT_MAX, 0.01f, sboxwidth);
    UIColumnHeader("Transport Scale", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->scale), 0, FLT_MAX, 0.01f, sboxwidth);
    UIColumnHeader("Subsurface Ratio", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->subsurface), 0, 1.0f, 0.01f, sboxwidth);
    UIMoveCursor(0, 35);
    UIColumnHeader("Lighting Model", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(sboxwidth, 4, LightModelLabels(), DropdownSelectLightModel, matref);
    UIMoveCursor(0, 15);
    UIDividerLabeled(width, "Textures");
    UIMoveCursor(0, 5);
    for (size_t k = 0; k < MATERIAL_TEXTURE_SLOTS; k++) g_slot_refs[k] = (MaterialSlotRef){ matref, k };
    #define TEXTURE_SLOT_ROW(k) { \
        UIColumnHeader(MaterialTextureSlotLabels()[k], LEFT_COLUMN_WIDTH); \
        UIDropdownMenu(sboxwidth, NumTextures() + 1, TextureHandleLabels(), DropdownSelectMaterialTexture, &(g_slot_refs[k])); }
    TEXTURE_SLOT_ROW(TEXSLOT_DIFFUSE);
    TEXTURE_SLOT_ROW(TEXSLOT_SPECULAR);
    TEXTURE_SLOT_ROW(TEXSLOT_NORMAL);
    TEXTURE_SLOT_ROW(TEXSLOT_EMISSION);
    TEXTURE_SLOT_ROW(TEXSLOT_AMBIENT);
    TEXTURE_SLOT_ROW(TEXSLOT_SHINY);
    TEXTURE_SLOT_ROW(TEXSLOT_ABSORBTION);
    TEXTURE_SLOT_ROW(TEXSLOT_SCATTERING);
    TEXTURE_SLOT_ROW(TEXSLOT_DISPERSION);
    TEXTURE_SLOT_ROW(TEXSLOT_IOR);
    TEXTURE_SLOT_ROW(TEXSLOT_ANISOTROPY);
    TEXTURE_SLOT_ROW(TEXSLOT_SCALE);
    TEXTURE_SLOT_ROW(TEXSLOT_SUBSURFACE);
    #undef TEXTURE_SLOT_ROW
    if (edited) UpdateMaterials();
    if (emission_edited) UpdateEmissives();
    if (was_emissive != is_emissive) RefreshEmissives();
}

static void DrawEditLight(SceneLight* lref, char* name, float width, float height) {
    BOOL edited = FALSE;
    int light_type = 0;
    if (lref->mode == LIGHT_MODE_IBL) {
        light_type = 3;
    } else if (lref->direction[0] == 0 && lref->direction[1] == 0 && lref->direction[2] == 0) {
        light_type = 2;
    } else if (lref->angle != 0) {
        light_type = 1;
    }
    UIMoveCursor((width - 20 - UITextWidth("Edit Light (%s)", g_light_types[light_type])) / 2.0f, 0);
    UIDrawText("Edit Light (%s)", g_light_types[light_type]);
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_LIGHT_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 15);
    UIColumnHeader("Position", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(lref->position[0]), &(lref->position[1]), &(lref->position[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Intensity", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(lref->color[0]), &(lref->color[1]), &(lref->color[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 1.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Direction", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(lref->direction[0]), &(lref->direction[1]), &(lref->direction[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIMoveCursor(0, 35);
    float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
    UIColumnHeader("Penumbra", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(lref->penumbra), 0, 1.0f, 0.01f, sboxwidth);
    UIColumnHeader("Opening Angle", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(lref->angle), 0, FLT_MAX, 0.1f, sboxwidth);
    UIMoveCursor(0, 15);
    UIDividerLabeled(width, "Texture");
    UIMoveCursor(0, 5);
    UIColumnHeader("Mode", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(sboxwidth, LIGHT_MODE_COUNT, LightModeLabels(), DropdownSelectLightMode, lref);
    UIColumnHeader(lref->mode == LIGHT_MODE_IBL ? "Environment Map" : "Projected Texture", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(sboxwidth, NumTextures() + 1, TextureHandleLabels(), DropdownSelectLightTexture, lref);
    UIColumnHeader(lref->mode == LIGHT_MODE_IBL ? "Yaw" : "Roll", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(lref->rotation), -FLT_MAX, FLT_MAX, 0.5f, sboxwidth);
    if (lref->mode == LIGHT_MODE_PROJECTOR && light_type == 0) {
        UIColumnHeader("Projection Width", LEFT_COLUMN_WIDTH);
        edited |= UIDragFloat(&(lref->projsize), 0.0f, FLT_MAX, 0.05f, sboxwidth);
    }
    if (edited) {
        UpdateLights();
        if (lref->mode != LIGHT_MODE_STANDARD) ResetAccumulation();
    }
}

static void DrawEditMesh(MeshDescriptor* md, char* name, float width, float height) {
    BOOL edited = FALSE;
    UIMoveCursor((width - 20 - UITextWidth("Edit Object")) / 2.0f, 0);
    UIDrawText("Edit Object");
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_MESH_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 15);
    if (md->pose != (uint32_t)-1 && md->pose / MAX_BONES < NumAnimations()) {
        MeshAnimation* anim = AnimationReference(md->pose / MAX_BONES);
        float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
        UIDividerLabeled(width, "Animation");
        UIMoveCursor(0, 5);
        UIColumnHeader("Rig", LEFT_COLUMN_WIDTH);
        UIDrawText("%zu bones, %zu clips", anim->skeleton.bonecount, anim->animations.size);
        if (anim->animations.size > 0) {
            UIColumnHeader("Animation", LEFT_COLUMN_WIDTH);
            UIDropdownMenu(sboxwidth, anim->animations.size, anim->names.data, DropdownSelectAnimation, anim);
        }
        UIColumnHeader("Enable", LEFT_COLUMN_WIDTH);
        UICheckbox(&(anim->enable));
        UIColumnHeader("Follow Timeline", LEFT_COLUMN_WIDTH);
        if (UICheckbox(&(anim->synced))) anim->dirty = TRUE;
        UIColumnHeader("Loop", LEFT_COLUMN_WIDTH);
        UICheckbox(&(anim->looping));
        if (!anim->synced) {
            UIColumnHeader("Transition", LEFT_COLUMN_WIDTH);
            UIDragFloat(&(anim->bduration), 0, FLT_MAX, 0.01f, sboxwidth);
            UIColumnHeader("Time", LEFT_COLUMN_WIDTH);
            UIDragFloat(&(anim->time), 0, FLT_MAX, 0.01f, sboxwidth);
            UIColumnHeader("Play", LEFT_COLUMN_WIDTH);
            UICheckbox(&(anim->playing));
        }
        if (RigActiveMesh() != g_edit_item_index || !RigViewportActive()) {
            UIMoveCursor(0, 5);
            if (UIButton("Pose This Rig (Shift+A)", width - 20)) {
                RigSetActiveMesh(g_edit_item_index);
                RigSetMode(RIG_MODE_POSE);
            }
        }
    } else if (!(md->end < md->start || md->end == (VertexID)-1)) {
        UIMoveCursor(0, 5);
        if (UIButton("Create Rig", width - 20)) {
            if (RigCreateForMesh(g_edit_item_index)) RigSetMode(RIG_MODE_EDIT);
        }
    }
    UIMoveCursor(0, 15);
    UIColumnHeader("Translation", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(md->translate[0]), &(md->translate[1]), &(md->translate[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Rotation", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(md->rotate[0]), &(md->rotate[1]), &(md->rotate[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Scale", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(md->scale[0]), &(md->scale[1]), &(md->scale[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 1.0f }, width - LEFT_COLUMN_WIDTH);
    UIMoveCursor(0, 15);
    UIColumnHeader("Disable", LEFT_COLUMN_WIDTH);
    BOOL disabled = md->disabled != 0;
    UICheckbox(&disabled);
    edited |= disabled != (md->disabled != 0);
    md->disabled = disabled;
    if (edited) UpdateObjectTransform(g_edit_item_index);
    if (!(md->tend < md->tstart || md->tend == (TriangleID)-1)) {
        float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
        UIMoveCursor(0, 15);
        UIDividerLabeled(width, "Texturing");
        UIMoveCursor(0, 5);
        UIColumnHeader("Texture Filtering", LEFT_COLUMN_WIDTH);
        UIDropdownMenu(sboxwidth, TEXFILTER_COUNT, TextureFilterLabels(), DropdownSelectObjectFilter, md);
        if (md->filter == TEXFILTER_ANISOTROPIC) {
            UIColumnHeader("Max Anisotropy", LEFT_COLUMN_WIDTH);
            uint32_t aniso = md->anisotaps == 0 ? DEFAULT_MAX_ANISOTROPY : md->anisotaps;
            if (UIDragUInt(&aniso, 1, 64, 1, sboxwidth)) {
                md->anisotaps = aniso;
                UpdateMeshes();
                ResetAccumulation();
            }
        }
        UIColumnHeader("Recursive Ray Diff.", LEFT_COLUMN_WIDTH);
        BOOL raydiff = md->raydiff != 0;
        if (UICheckbox(&raydiff)) {
            md->raydiff = raydiff ? 1 : 0;
            UpdateMeshes();
            ResetAccumulation();
        }
        UIDrawSubtleText(raydiff ? "Secondary hits filter with propagated differentials" : "Secondary hits sample the base level");
    }
    if (!(md->tend < md->tstart || md->tend == (TriangleID)-1)) {
        SubsurfaceCloudInfo info = { 0 };
        BOOL linked = QuerySubsurfaceCloudInfo(g_edit_item_index, &info);
        if (linked) {
            UIColumnHeader("Subsurface cloud", LEFT_COLUMN_WIDTH);
            BOOL enabled = info.enabled;
            if (UICheckbox(&enabled)) SubsurfaceSetCloudEnabled(g_edit_item_index, enabled);
        }
    }
}

static char g_bone_name[256] = { 0 };
static size_t g_bone_name_for = (size_t)-1;
static size_t g_bone_name_anim = (size_t)-1;
static vec3 g_bone_euler = { 0 };
static float g_bone_euler_quat[4] = { 0, 0, 0, 1 };
static uint32_t g_ik_chain = 2;

static size_t DropdownSelectBoneParent(void* data, size_t index, BOOL cancel) {
    MeshAnimation* anim = RigActive();
    size_t bone = g_edit_item_index;
    if (!anim || bone >= anim->skeleton.bonecount) return 0;
    if (index != (size_t)-1 && !cancel) RigSetParent(bone, index == 0 ? (size_t)-1 : index - 1);
    size_t p = anim->skeleton.bones[bone].parent;
    return p < anim->skeleton.bonecount ? p + 1 : 0;
}

static size_t DropdownSelectIKTarget(void* data, size_t index, BOOL cancel) {
    MeshAnimation* anim = RigActive();
    size_t c = (size_t)data;
    if (!anim || c >= anim->numik) return 0;
    IKConstraint* ik = &(anim->ik[c]);
    if (index != (size_t)-1 && !cancel) {
        ik->target = index == 0 ? IK_NO_BONE : (uint32_t)(index - 1);
        anim->dirty = TRUE;
    }
    return ik->target < anim->skeleton.bonecount ? ik->target + 1 : 0;
}

static size_t DropdownSelectIKPole(void* data, size_t index, BOOL cancel) {
    MeshAnimation* anim = RigActive();
    size_t c = (size_t)data;
    if (!anim || c >= anim->numik) return 0;
    IKConstraint* ik = &(anim->ik[c]);
    if (index != (size_t)-1 && !cancel) {
        ik->pole = index == 0 ? IK_NO_BONE : (uint32_t)(index - 1);
        anim->dirty = TRUE;
    }
    return ik->pole < anim->skeleton.bonecount ? ik->pole + 1 : 0;
}

static BOOL BoneButton(const char* label, float w) {
    return UIButton(label, w) && AnimPanelHovered("Edit Selected") && !InputsBlocked();
}

static void DrawEditBone(size_t bone, float width, float height) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) {
        UIDrawSubtleText("The selected bone no longer exists");
        return;
    }
    Bone* b = &(anim->skeleton.bones[bone]);
    float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
    UIMoveCursor((width - 20 - UITextWidth("Edit Bone")) / 2.0f, 0);
    UIDrawText("Edit Bone");
    UIMoveCursor(0, 15);
    size_t aidx = RigActiveAnimation();
    if (g_bone_name_for != bone || g_bone_name_anim != aidx) {
        snprintf(g_bone_name, sizeof(g_bone_name), "%s", b->name);
        g_bone_name_for = bone;
        g_bone_name_anim = aidx;
    }
    if (UITextInput("Name", g_bone_name, sizeof(g_bone_name) - 1, width - 20, FALSE)) {
        RigRenameBone(bone, g_bone_name);
        g_bone_name_for = (size_t)-1;
    }
    UIDrawSubtleText("press enter to rename (keys follow the bone)");
    UIMoveCursor(0, 5);
    size_t nitems = 0;
    char** items = AnimBoneItems(TRUE, &nitems);
    UIColumnHeader("Parent", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(sboxwidth, nitems, items, DropdownSelectBoneParent, NULL);
    BOOL deform = (b->flags & BONE_FLAG_DEFORM) != 0;
    UIColumnHeader("Deform", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&deform)) RigSetBoneFlag(bone, BONE_FLAG_DEFORM, deform);
    BOOL connected = (b->flags & BONE_FLAG_CONNECTED) != 0;
    UIColumnHeader("Connected", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&connected)) RigSetBoneFlag(bone, BONE_FLAG_CONNECTED, connected);
    BOOL hidden = (b->flags & BONE_FLAG_HIDDEN) != 0;
    UIColumnHeader("Hidden", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&hidden)) RigSetBoneFlag(bone, BONE_FLAG_HIDDEN, hidden);

    // pose
    UIMoveCursor(0, 10);
    UIDividerLabeled(width, "Pose");
    UIMoveCursor(0, 5);
    BonePose pose;
    BoneCurrentPose(anim, bone, &pose);
    versor q;
    memcpy(q, pose.rotation, sizeof(versor));
    float qd = fabsf(q[0] * g_bone_euler_quat[0] + q[1] * g_bone_euler_quat[1] + q[2] * g_bone_euler_quat[2] + q[3] * g_bone_euler_quat[3]);
    if (qd < 0.99999f) {
        mat4 rm;
        glm_quat_mat4(q, rm);
        vec3 rad;
        glm_euler_angles(rm, rad);
        for (int i = 0; i < 3; i++) g_bone_euler[i] = glm_deg(rad[i]);
        memcpy(g_bone_euler_quat, q, sizeof(g_bone_euler_quat));
    }
    BOOL posed = FALSE;
    UIColumnHeader("Location", LEFT_COLUMN_WIDTH);
    posed |= UITriplet("xyz", UI_FLOATS, &(pose.location[0]), &(pose.location[1]), &(pose.location[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.01f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Rotation (deg)", LEFT_COLUMN_WIDTH);
    BOOL rotated = UITriplet("xyz", UI_FLOATS, &(g_bone_euler[0]), &(g_bone_euler[1]), &(g_bone_euler[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.5f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Scale", LEFT_COLUMN_WIDTH);
    posed |= UITriplet("xyz", UI_FLOATS, &(pose.scale[0]), &(pose.scale[1]), &(pose.scale[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.01f }, (UIMultiValue){ ._float = 1.0f }, width - LEFT_COLUMN_WIDTH);
    if (rotated) {
        vec3 rad = { glm_rad(g_bone_euler[0]), glm_rad(g_bone_euler[1]), glm_rad(g_bone_euler[2]) };
        mat4 rm;
        glm_euler_xyz(rad, rm);
        versor nq;
        glm_mat4_quat(rm, nq);
        memcpy(pose.rotation, nq, sizeof(pose.rotation));
        memcpy(g_bone_euler_quat, nq, sizeof(g_bone_euler_quat));
        posed = TRUE;
    }
    if (posed) {
        BoneSetPose(anim, bone, &pose);
        if (Timeline()->autokey) KeyInsert(anim, bone, Timeline()->frame, KEY_CHANNEL_ALL);
    }
    Animation* clip = CurrentClip(anim);
    BOOL keyed = clip && KeyExists(clip, b->name, AnimationFrameToTicks(clip, Timeline()->frame));
    UIColumnHeader("State", LEFT_COLUMN_WIDTH);
    UIDrawSubtleText("%s%s", keyed ? "keyed at this frame" : "not keyed at this frame", BoneIsPosed(anim, bone) ? ", unkeyed changes" : "");
    UIMoveCursor(0, 5);
    float half = (width - 30) / 2.0f;
    Vector2 row = UIGetCursor();
    if (BoneButton("Insert Key", half)) KeyInsert(anim, bone, Timeline()->frame, KEY_CHANNEL_ALL);
    UISetCursor(row.x + half + 10, row.y);
    if (BoneButton("Delete Key", half) && clip) {
        KeyDelete(clip, b->name, AnimationFrameToTicks(clip, Timeline()->frame));
        anim->dirty = TRUE;
    }
    row = UIGetCursor();
    if (BoneButton("Reset Pose", half)) {
        BonePose identity;
        PoseIdentity(&identity);
        BoneSetPose(anim, bone, &identity);
        if (Timeline()->autokey) KeyInsert(anim, bone, Timeline()->frame, KEY_CHANNEL_ALL);
    }
    UISetCursor(row.x + half + 10, row.y);
    if (BoneButton("Revert To Keys", half)) BoneClearOverride(anim, bone);

    // rest
    UIMoveCursor(0, 10);
    UIDividerLabeled(width, "Rest Pose");
    UIMoveCursor(0, 5);
    vec3 head, tail;
    RigRestHeadTail(bone, head, tail);
    float roll = glm_deg(RigBoneRoll(bone));
    BOOL rest = FALSE;
    UIColumnHeader("Head", LEFT_COLUMN_WIDTH);
    rest |= UITriplet("xyz", UI_FLOATS, &(head[0]), &(head[1]), &(head[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.01f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Tail", LEFT_COLUMN_WIDTH);
    rest |= UITriplet("xyz", UI_FLOATS, &(tail[0]), &(tail[1]), &(tail[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.01f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Roll (deg)", LEFT_COLUMN_WIDTH);
    rest |= UIDragFloat(&roll, -FLT_MAX, FLT_MAX, 0.5f, sboxwidth);
    UIColumnHeader("Length", LEFT_COLUMN_WIDTH);
    UIDrawText("%.4f", glm_vec3_distance(head, tail));
    if (rest) RigSetBoneRest(bone, head, tail, glm_rad(roll));
    UIDrawSubtleText("rest edits keep the mesh in place (weights are not recomputed)");

    // inverse kinematics
    UIMoveCursor(0, 10);
    UIDividerLabeled(width, "Inverse Kinematics");
    UIMoveCursor(0, 5);
    size_t c = RigFindIK(bone);
    if (c == (size_t)-1) {
        UIColumnHeader("Chain Length", LEFT_COLUMN_WIDTH);
        UIDragUInt(&g_ik_chain, 0, MAX_BONES, 1, sboxwidth);
        UIDrawSubtleText("bones from this one up (0 = up to the root)");
        if (BoneButton("Add IK With New Target", width - 20)) {
            size_t made = RigCreateIKWithTarget(bone, g_ik_chain);
            if (made != (size_t)-1) {
                RigSelectBone(anim->ik[made].target, FALSE);
                SetEditBone(anim->ik[made].target);
            }
        }
        size_t other = (size_t)-1;
        for (size_t o = 0; o < anim->skeleton.bonecount; o++) if (o != bone && RigBoneSelected(o)) { other = o; break; }
        if (other != (size_t)-1) {
            char label[320];
            snprintf(label, sizeof(label), "Add IK Targeting %.200s", anim->skeleton.bones[other].name);
            if (BoneButton(label, width - 20)) {
                size_t made = RigAddIK(bone, other);
                if (made != (size_t)-1) anim->ik[made].chain = g_ik_chain;
            }
        } else {
            UIDrawSubtleText("shift-select another bone to use it as the target");
        }
    } else {
        IKConstraint* ik = &(anim->ik[c]);
        BOOL edited = FALSE;
        BOOL enabled = ik->enabled != 0;
        UIColumnHeader("Enabled", LEFT_COLUMN_WIDTH);
        if (UICheckbox(&enabled)) { ik->enabled = enabled ? 1 : 0; edited = TRUE; }
        UIColumnHeader("Target", LEFT_COLUMN_WIDTH);
        UIDropdownMenu(sboxwidth, nitems, items, DropdownSelectIKTarget, (void*)c);
        UIColumnHeader("Pole", LEFT_COLUMN_WIDTH);
        UIDropdownMenu(sboxwidth, nitems, items, DropdownSelectIKPole, (void*)c);
        UIColumnHeader("Chain Length", LEFT_COLUMN_WIDTH);
        edited |= UIDragUInt(&(ik->chain), 0, MAX_BONES, 1, sboxwidth);
        UIColumnHeader("Iterations", LEFT_COLUMN_WIDTH);
        edited |= UIDragUInt(&(ik->iterations), 1, 256, 1, sboxwidth);
        UIColumnHeader("Influence", LEFT_COLUMN_WIDTH);
        edited |= UIDragFloat(&(ik->influence), 0.0f, 1.0f, 0.01f, sboxwidth);
        UIColumnHeader("Pole Angle (deg)", LEFT_COLUMN_WIDTH);
        edited |= UIDragFloat(&(ik->poleangle), -360.0f, 360.0f, 0.5f, sboxwidth);
        BOOL match = ik->matchrotation != 0;
        UIColumnHeader("Match Target Rot.", LEFT_COLUMN_WIDTH);
        if (UICheckbox(&match)) { ik->matchrotation = match ? 1 : 0; edited = TRUE; }
        if (edited) anim->dirty = TRUE;
        if (ik->target >= anim->skeleton.bonecount) UIDrawSubtleText("pick a target bone for the chain to reach");
        else if (SkeletonIsDescendant(&(anim->skeleton), ik->target, ik->bone)) UIDrawSubtleText("warning: the target is a child of the chain");
        UIMoveCursor(0, 5);
        if (ik->target < anim->skeleton.bonecount && BoneButton("Select Target", width - 20)) {
            RigSelectBone(ik->target, FALSE);
            SetEditBone(ik->target);
        }
        if (BoneButton("Remove IK", width - 20)) RigRemoveIK(c);
    }
}

static void DrawEditTexture(size_t index, float width, float height) {
    SceneTexture* tex = TextureReference(index);
    TextureSettings* st = &(tex->settings);
    float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
    UIMoveCursor((width - 20 - UITextWidth("Edit Texture")) / 2.0f, 0);
    UIDrawText("Edit Texture");
    UIMoveCursor(0, 15);
    UITextInput("Name", TextureName(index), MAX_TEXTURE_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 5);
    UIDividerLabeled(width, "Statistics");
    UIMoveCursor(0, 5);
    UIColumnHeader("Format", LEFT_COLUMN_WIDTH);
    DrawRectangle(UIGetCursor().x, UIGetCursor().y, sboxwidth, 20, (Color){ 0, 0, 0, 245 });
    UIDrawText("%u x %u %s", st->width, st->height, st->hdr ? "HDR (rgba16f)" : "LDR (rgba8)");
    UIColumnHeader("Mip levels", LEFT_COLUMN_WIDTH);
    DrawRectangle(UIGetCursor().x, UIGetCursor().y, sboxwidth, 20, (Color){ 0, 0, 0, 245 });
    UIDrawText("%u", st->levels);
    UIColumnHeader("Size", LEFT_COLUMN_WIDTH);
    DrawRectangle(UIGetCursor().x, UIGetCursor().y, sboxwidth, 20, (Color){ 0, 0, 0, 245 });
    UIDrawText("%.2f MB", TextureBytes(index) / (1024.0 * 1024.0));
    UIMoveCursor(0, 15);
    UIDividerLabeled(width, "Sampling");
    UIMoveCursor(0, 5);
    UIColumnHeader("sRGB Color Data", LEFT_COLUMN_WIDTH);
    if (st->hdr) UIDrawSubtleText("linear (HDR)");
    else if (UICheckbox(&(st->srgb))) UpdateTextures();
    UIColumnHeader("Wrap Mode", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(sboxwidth, TEXWRAP_COUNT, TextureWrapLabels(), DropdownSelectTextureWrap, tex);
    UIColumnHeader("Normal Map", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&(st->normalmap))) {
        if (st->normalmap) st->srgb = FALSE;
        UpdateTextures();
    }
    if (st->normalmap) {
        UIColumnHeader("Flip Green (DX)", LEFT_COLUMN_WIDTH);
        if (UICheckbox(&(st->flipgreen))) UpdateTextures();
    }
    UIMoveCursor(0, 15);
    UIDividerLabeled(width, "Mipmaps");
    UIMoveCursor(0, 5);
    UIColumnHeader("Downsampling", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(sboxwidth, MIPFILTER_COUNT, MipFilterLabels(), DropdownSelectMipFilter, tex);
    UIColumnHeader("Sharpness", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(st->sharpness), 0.25f, 4.0f, 0.01f, sboxwidth);
    UIColumnHeader("Linear Space", LEFT_COLUMN_WIDTH);
    if (!st->srgb || st->hdr || st->normalmap) UIDrawSubtleText(st->normalmap ? "renormalized normals" : "already linear");
    else UICheckbox(&(st->linearmips));
    UIColumnHeader("Max Levels", LEFT_COLUMN_WIDTH);
    UIDragUInt(&(st->maxlevels), 0, MAX_TEXTURE_LEVELS, 1, sboxwidth);
    UIMoveCursor(0, 5);
    if (UIButton(st->levels > 1 ? "Regenerate Mipmaps" : "Generate Mipmaps", width - 20)) {
        GenerateTextureMipmaps(index);
        g_preview_level = 0;
    }
    UIMoveCursor(0, 5);
    if (st->levels <= 1) DisableUI();
    if (UIButton("Clear Mipmaps", width - 20)) {
        ClearTextureMipmaps(index);
        g_preview_level = 0;
    }
    EnableUI();
    UIMoveCursor(0, 15);
    UIDividerLabeled(width, "Preview");
    UIMoveCursor(0, 5);
    if (g_preview_level >= st->levels) g_preview_level = st->levels - 1;
    char buf[256] = { 0 };
    TextureLevel* lv = &(tex->mips[g_preview_level]);
    sprintf(buf, "Level (%u x %u)", lv->width, lv->height);
    UIColumnHeader(buf, LEFT_COLUMN_WIDTH);
    if (st->levels <= 1) DisableUI();
    UIDragUInt(&g_preview_level, 0, st->levels - 1, 1, sboxwidth);
    EnableUI();
    UIMoveCursor(0, 15);
    Texture2D* preview = TexturePreview(index, g_preview_level);
    if (preview && preview->id != 0 && lv->width > 0 && lv->height > 0) {
        float maxw = width - 20;
        float maxh = 360.0f;
        float scale = maxw / (float)lv->width;
        if ((float)lv->height * scale > maxh) scale = maxh / (float)lv->height;
        float dw = (float)lv->width * scale;
        float dh = (float)lv->height * scale;
        Vector2 at = UIGetCursor();
        at.x += (maxw - dw) / 2.0f;
        DrawRectangle(at.x - 1, at.y - 1, dw + 2, dh + 2, GRAY);
        DrawTexturePro(*preview, (Rectangle){ 0, 0, (float)preview->width, (float)preview->height },
            (Rectangle){ at.x, at.y, dw, dh }, (Vector2){ 0, 0 }, 0.0f, WHITE);
        UIMoveCursor(0, dh + 10);
    }
}

static void DrawEditCamera(SceneCamera* cref, char* name, float width, float height) {
    UIMoveCursor((width - 20 - UITextWidth("Edit Camera")) / 2.0f, 0);
    UIDrawText("Edit Camera");
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_CAMERA_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 3);
    if (g_edit_item_index == PrimaryCameraID()) DisableUI();
    if (UIButton("Set Primary", width - 20)) {
        SetCamera(g_edit_item_index);
    }
    EnableUI();
    UIMoveCursor(0, 20);
    UIColumnHeader("Position", LEFT_COLUMN_WIDTH);
    UITriplet("xyz", UI_FLOATS, &(cref->core.position[0]), &(cref->core.position[1]), &(cref->core.position[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Look", LEFT_COLUMN_WIDTH);
    UITriplet("xyz", UI_FLOATS, &(cref->core.look[0]), &(cref->core.look[1]), &(cref->core.look[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Up", LEFT_COLUMN_WIDTH);
    UITriplet("xyz", UI_FLOATS, &(cref->core.up[0]), &(cref->core.up[1]), &(cref->core.up[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIMoveCursor(0, 25);
    UIColumnHeader("Preview Only", LEFT_COLUMN_WIDTH);
    BOOL preview = !(cref->config.flags & PATHTRACE_SHADER_FLAG);
    if (UICheckbox(&preview)) {
        cref->config.flags = preview ? PREVIEW_PIPELINE_FLAGS : PATHTRACE_PIPELINE_FLAGS;
    }
    UIColumnHeader("Grid", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.grid));
    UIColumnHeader("Wireframe", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.wireframe));
    UIColumnHeader("Smooth Normals", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.normals));
    UIColumnHeader("BVH Culling", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.screenspace));
    UIColumnHeader("Splat Shading", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.splatshadows));
    UIColumnHeader("Splat Occlusion", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&(cref->config.splatocclusion))) cref->config.reset = TRUE;
    UIColumnHeader("Direct Lighting", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.direct));
    UIColumnHeader("Direct Lighting Only", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.directonly));
    UIColumnHeader("Scene Lights", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.scenelighting));
    UIColumnHeader("Scene Lights Only", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.scenelightingonly));
    UIColumnHeader("Scene Light Shadows", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.scenelightshadows));
    UIColumnHeader("Spectral Coloring", LEFT_COLUMN_WIDTH);
    UICheckbox(&(cref->config.spectral));
    UIColumnHeader("Subsurface Splats", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&(cref->config.gsssenabled))) cref->config.reset = TRUE;
    UIColumnHeader("SSS Exit Lighting", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&(cref->config.exitsss))) cref->config.reset = TRUE;

    UIMoveCursor(0, 15);
    float sboxwidth = width - 20 - 160;
    UIColumnHeader("Debug Mode", LEFT_COLUMN_WIDTH);
    size_t pp = PrimaryCameraID();
    SetCamera(g_edit_item_index);
    UIDropdownMenu(sboxwidth, 8, DebugModeLabels(), DropdownSelectDebugMode, NULL);
    SetCamera(pp);
    UIColumnHeader("Max Bounces", LEFT_COLUMN_WIDTH);
    UIDragSize(&(cref->config.maxbounces), 0, 999999999, 1, sboxwidth);
    UIColumnHeader("Max SSS Bounces", LEFT_COLUMN_WIDTH);
    UIDragSize(&(cref->config.maxsssbounces), 0, 999999999, 1, sboxwidth);
    UIColumnHeader("Frame Multiplier", LEFT_COLUMN_WIDTH);
    UIDragSize(&(cref->config.multiplier), 0, 999999999, 1, sboxwidth);
    UIColumnHeader("Whitepoint", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(cref->config.whitepoint), 0.01f, 999999999.0f, 0.1f, sboxwidth);
    UIColumnHeader("Gamma", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(cref->config.gamma), 0.01f, 999999999.0f, 0.1f, sboxwidth);

    UIMoveCursor(0, 20.0f);
    UIColumnHeader("Denoise", LEFT_COLUMN_WIDTH);
    UIMoveCursor(-2, 0);
    UICheckbox(&(cref->config.denoise));
    if (!cref->config.denoise) DisableUI();
    UIColumnHeader("Denoise Iterations", LEFT_COLUMN_WIDTH);
    UIDragSize(&(cref->config.denoiseiterations), 0, 10, 1, sboxwidth);
    UIColumnHeader("Color Sigma", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(cref->config.denoisecolor), 0.0f, 100.0f, 0.1f, sboxwidth);
    UIColumnHeader("Normal Sigma", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(cref->config.denoisenormal), 0.0f, 512.0f, 1.0f, sboxwidth);
    UIColumnHeader("Depth Sigma", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(cref->config.denoisedepth), 0.01f, 100.0f, 0.1f, sboxwidth);
    EnableUI();

    SimpleCamera c = cref->core;
    SimpleCamera oldc = cref->core;
    BOOL used = FALSE;
    UIMoveCursor(0, 20.0f);
    UIColumnHeader("Aperature", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(c.aperature), 0.0f, 999999999.0f, 0.01f, sboxwidth);
    used |= UIWasJustUsed();
    UIColumnHeader("Focus", LEFT_COLUMN_WIDTH);
    UIDragFloat(&(c.focus), 0.0f, 999999999.0f, 0.01f, sboxwidth);
    used |= UIWasJustUsed();
    UIColumnHeader("Orthographic", LEFT_COLUMN_WIDTH);
    UIMoveCursor(-2, 0);
    UICheckbox(&(c.ortho));
    if (c.ortho != oldc.ortho) {
        vec3 offset;
        glm_vec3_sub(c.position, c.look, offset);
        float dist = glm_vec3_norm(offset);
        float half_tan = tanf(glm_rad(CLAMP(c.fov, 1.0f, 170.0f)) * 0.5f);
        if (c.ortho) {
            float framed = 2.0f * dist * half_tan;
            if (framed > 1e-4f) c.orthosize = framed;
        } else if (dist > 1e-6f && c.orthosize > 1e-4f) {
            float newdist = c.orthosize / (2.0f * half_tan);
            glm_vec3_scale(offset, newdist / dist, offset);
            glm_vec3_add(c.look, offset, c.position);
        }
    }
    if (c.ortho) {
        UIColumnHeader("Ortho Size", LEFT_COLUMN_WIDTH);
        UIDragFloat(&(c.orthosize), 0.001f, 999999999.0f, 0.05f, sboxwidth);
    } else {
        UIColumnHeader("FOV", LEFT_COLUMN_WIDTH);
        UIDragFloat(&(c.fov), 0.0f, 180.0f, 0.1f, sboxwidth);
    }
	if (memcmp(&c, &oldc, sizeof(SimpleCamera))) cref->core = c;
    cref->config.showdof = used;
    UIColumnHeader("Resolution", LEFT_COLUMN_WIDTH);
    size_t w = cref->config.resolution.x;
    size_t h = cref->config.resolution.y;
    UIDoublet("wh", UI_SIZES, &w, &h, (UIMultiValue){ ._size = 10 }, (UIMultiValue){ ._size = 8000 }, (UIMultiValue){ ._size = 1 }, (UIMultiValue){ ._size = 900 }, width - LEFT_COLUMN_WIDTH - 10);
    cref->config.resolution.x = w;
    cref->config.resolution.y = h;
}

static void DrawEditPanel(float width, float height) {
    float cursory = UIGetCursor().y;
    if (g_item_selected) {
        if (g_edit_type == EDIT_MATERIAL) {
            SurfaceMaterial* matref = MaterialReference(g_edit_item_index);
            DrawEditMaterial(matref, MaterialName(g_edit_item_index), width, height);
        } else if (g_edit_type == EDIT_LIGHT) {
            SceneLight* lref = LightReference(g_edit_item_index);
            DrawEditLight(lref, LightName(g_edit_item_index), width, height);
        } else if (g_edit_type == EDIT_SINGLE_TRIANGLE) {
            BOOL edited = FALSE;
            Triangle* tref = TriangleReference(g_edit_item_index);
            UIMoveCursor((width - 20 - UITextWidth("Edit Face")) / 2.0f, 0);
            UIDrawText("Edit Face");
            UIMoveCursor(0, 15);
            UIDividerLabeled(width, "Vertices");
            UIMoveCursor(0, 5);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->a)[0], &VertexReference(tref->a)[1], &VertexReference(tref->a)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->b)[0], &VertexReference(tref->b)[1], &VertexReference(tref->b)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->c)[0], &VertexReference(tref->c)[1], &VertexReference(tref->c)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            UIMoveCursor(0, 15);
            UIDividerLabeled(width, "Move Face");
            UIMoveCursor(0, 5);
            vec3 old_a;
            glm_vec3_copy(VertexReference(tref->a), old_a);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->a)[0], &VertexReference(tref->a)[1], &VertexReference(tref->a)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            UIMoveCursor(0, 35);
            UIColumnHeader("Material", LEFT_COLUMN_WIDTH);
            float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
            UIDropdownMenu(sboxwidth, NumMaterials(), MaterialNameReference(0), DropdownSelectMaterial, tref);
            UIMoveCursor(0, 15);
            UIDividerLabeled(width, "UVs");
            UIMoveCursor(0, 5);
            if (TriangleHasUVs(*tref) && tref->at < NumUVs() && tref->bt < NumUVs() && tref->ct < NumUVs()) {
                // uvs can be shared with neighbouring faces, like vertices
                BOOL uvedited = FALSE;
                float* ua = UVReference(tref->at);
                float* ub = UVReference(tref->bt);
                float* uc = UVReference(tref->ct);
                uvedited |= UIDoublet("uv", UI_FLOATS, &(ua[0]), &(ua[1]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.01f }, (UIMultiValue){ ._float = 0.0f }, width);
                uvedited |= UIDoublet("uv", UI_FLOATS, &(ub[0]), &(ub[1]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.01f }, (UIMultiValue){ ._float = 0.0f }, width);
                uvedited |= UIDoublet("uv", UI_FLOATS, &(uc[0]), &(uc[1]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.01f }, (UIMultiValue){ ._float = 0.0f }, width);
                UIDrawSubtleText("uv indices %u, %u, %u", tref->at, tref->bt, tref->ct);
                if (uvedited) UpdateUVs();
                UIMoveCursor(0, 5);
                if (UIButton("Remove UVs", width - 20)) {
                    tref->at = NO_UV;
                    tref->bt = NO_UV;
                    tref->ct = NO_UV;
                    UpdateTriangles();
                }
            } else {
                UIDrawSubtleText("No uvs - the material's plain values are used");
                if (UIButton("Add UVs", width - 20)) {
                    vec2 defaults[3] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
                    tref->at = (uint32_t)NumUVs();
                    tref->bt = tref->at + 1;
                    tref->ct = tref->at + 2;
                    for (size_t k = 0; k < 3; k++) SubmitUV(defaults[k]);
                    UpdateTriangles();
                }
            }
            vec3 adiff;
            glm_vec3_sub(VertexReference(tref->a), old_a, adiff);
            glm_vec3_add(VertexReference(tref->b), adiff, VertexReference(tref->b));
            glm_vec3_add(VertexReference(tref->c), adiff, VertexReference(tref->c));
            if (edited) UpdateVertices();
        } else if (g_edit_type == EDIT_SINGLE_VERTEX) {
            BOOL edited = FALSE;
            float* vref = VertexReference(g_edit_item_index);
            UIMoveCursor((width - 20 - UITextWidth("Edit Vertex")) / 2.0f, 0);
            UIDrawText("Edit Vertex");
            UIMoveCursor(0, 15);
            UIColumnHeader("Position", LEFT_COLUMN_WIDTH);
            edited |= UITriplet("xyz", UI_FLOATS, &(vref[0]), &(vref[1]), &(vref[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
            UIColumnHeader("(Deform) Lock", LEFT_COLUMN_WIDTH);
            BOOL locked = VertexLocked(g_edit_item_index);
            UIMoveCursor(-4, 2);
            UICheckbox(&locked);
            edited |= locked != VertexLocked(g_edit_item_index);
            if (edited) {
                if (locked) LockVertex(g_edit_item_index);
                else UnlockVertex(g_edit_item_index);
                UpdateVertices();
            }
        } else if (g_edit_type == EDIT_SINGLE_SOURCE) {
            BOOL edited = FALSE;
            FluidSimulation* fsim = &(RendererGeometry()->fluid);
            FluidSource* sourceref = SourceReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Source")) / 2.0f, 0);
            UIDrawText("Edit Source");
            UIMoveCursor(0, 15);
            UITextInput("Name", *(SourceNameReference(g_edit_item_index)), MAX_SOURCE_NAME_SIZE, width - 20, FALSE);
            UIMoveCursor(0, 15);
            float sboxwidth = width - 20 - 140;
            UIDrawText("Density");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(sourceref->density), 0, FLT_MAX, 0.01f, sboxwidth);
            UIMoveCursor(0, 5);
            UIDrawText("Lifetime");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(sourceref->lifetime), 0, FLT_MAX, 0.01f, sboxwidth);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Size") / 2) - 10, 0);
            UIDrawText("Size");
            UIMoveCursor(0, 5);
            UIDrawText("w");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(sourceref->width), 0, fsim->width - sourceref->x, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("h");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(sourceref->height), 0, fsim->height - sourceref->y, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("l");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(sourceref->length), 0, fsim->length - sourceref->z, 1, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Origin") / 2) - 10, 0);
            UIDrawText("Origin");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(sourceref->x), 0, fsim->width - sourceref->width, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(sourceref->y), 0, fsim->height - sourceref->height, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(sourceref->z), 0, fsim->length - sourceref->length, 1, component_width);
            UIMoveCursor(0, 5);
            if (edited) UpdateSimulation();
        } else if (g_edit_type == EDIT_SINGLE_FORCE) {
            BOOL edited = FALSE;
            FluidSimulation* fsim = &(RendererGeometry()->fluid);
            FluidForce* forceref = ForceReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Force")) / 2.0f, 0);
            UIDrawText("Edit Force");
            UIMoveCursor(0, 15);
            UITextInput("Name", *(ForceNameReference(g_edit_item_index)), MAX_FORCE_NAME_SIZE, width - 20, FALSE);
            UIMoveCursor(0, 15);
            UIDrawText("Global Force");
            UIMoveCursor(140, -20);
            UICheckbox(&(forceref->global));
            if (forceref->global) DisableUI();
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Size") / 2) - 10, 0);
            UIDrawText("Size");
            UIMoveCursor(0, 5);
            UIDrawText("w");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(forceref->width), 0, fsim->width - forceref->x, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("h");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(forceref->height), 0, fsim->height - forceref->y, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("l");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(forceref->length), 0, fsim->length - forceref->z, 1, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Origin") / 2) - 10, 0);
            UIDrawText("Origin");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(forceref->x), 0, fsim->width - forceref->width, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(forceref->y), 0, fsim->height - forceref->height, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(forceref->z), 0, fsim->length - forceref->length, 1, component_width);
            UIMoveCursor(0, 15);
            EnableUI();
            UIMoveCursor((width / 2) - (UITextWidth("Force Vector") / 2) - 10, 0);
            UIDrawText("Force Vector");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(forceref->force[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(forceref->force[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(forceref->force[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            if (edited) UpdateSimulation();
        } else if (g_edit_type == EDIT_MESH) {
            MeshDescriptor* md = MeshReference(g_edit_item_index);
            DrawEditMesh(md, *(MeshNameReference(g_edit_item_index)), width, height);
        } else if (g_edit_type == EDIT_CAMERA) {
            SceneCamera* cref = GetCamera(g_edit_item_index);
            DrawEditCamera(cref, *(CameraNameReference(g_edit_item_index)), width, height);
        } else if (g_edit_type == EDIT_TEXTURE) {
            DrawEditTexture(g_edit_item_index, width, height);
        } else if (g_edit_type == EDIT_BONE) {
            DrawEditBone(g_edit_item_index, width, height);
        } else {
            EZ_FATAL("Unhandled edit type detected");
        }
        float drawnheight = UIGetCursor().y - cursory;
        if (drawnheight > height - 90) {
            UIMoveCursor(0, 20);
            if (UIButton("Export", width - 20)) ExportEditTarget();
            UIMoveCursor(0, 5);
            if (UIButton("Delete", width - 20)) DeleteEditTarget();
        } else {
            Vector2 backup = UIGetCursor();
            UISetCursor(10, height - 80);
            if (UIButton("Export", width - 20)) ExportEditTarget();
            UIMoveCursor(0, 5);
            if (UIButton("Delete", width - 20)) DeleteEditTarget();
            UISetCursor(backup.x, backup.y);
        }
    } else {
        UISetCursor((width - UITextWidth("No Selected Element"))/2.0f, height / 2.0f - 20);
        UIDrawText("No Selected Element");
    }
}

BOOL GetEditTarget(EditType* type, size_t* index) {
    if (!g_item_selected) return FALSE;
    *type = g_edit_type;
    *index = g_edit_item_index;
    return TRUE;
}

void FixDeleteEditTarget(EditType type, size_t index) {
    if (type == EDIT_MESH) RigMeshDeleted(index);
    if (!g_item_selected) return;
    if (index > g_edit_item_index) return;
    if (type == g_edit_type) {
        if (index == g_edit_item_index) {
            DeselectEditTarget();
        } else {
            g_edit_item_index--;
        }
    }
}

void SetEditCamera(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_CAMERA;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void SetEditMaterial(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_MATERIAL;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void SetEditLight(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_LIGHT;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)index);
}

void SetEditTriangle(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_TRIANGLE;
    SetSelectedVertex((VertexID)-1);
    SetSelectedTriangle(index);
    SetSelectedLight((LightID)-1);
}

void SetEditVertex(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_VERTEX;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex(index);
    SetSelectedLight((LightID)-1);
}

void SetEditForce(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_FORCE;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void SetEditSource(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_SOURCE;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void SetEditMesh(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_MESH;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void SetEditTexture(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_TEXTURE;
    g_preview_level = 0;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void SetEditBone(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_BONE;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void DeselectEditTarget() {
    g_item_selected = FALSE;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
    SetSelectedLight((LightID)-1);
}

void DeleteEditTarget() {
    if (!g_item_selected) {
        logwarn("Nothing selected to delete");
        return;
    }
    switch (g_edit_type) {
        case EDIT_MATERIAL:
            if (!DeleteMaterial(g_edit_item_index)) return;
            break;
        case EDIT_LIGHT:
            if (!DeleteLight(g_edit_item_index)) return;
            break;
        case EDIT_SINGLE_TRIANGLE:
            if (!DeleteTriangle(g_edit_item_index)) return;
            break;
        case EDIT_SINGLE_VERTEX:
            if (!DeleteVertex(g_edit_item_index)) return;
            break;
        case EDIT_MESH:
            if (!DeleteMesh(g_edit_item_index)) return;
            break;
        case EDIT_CAMERA:
            if (!DeleteCamera(g_edit_item_index)) return;
            break;
        case EDIT_TEXTURE:
            if (!DeleteTexture(g_edit_item_index)) return;
            break;
        case EDIT_BONE:
            if (RigGetMode() != RIG_MODE_EDIT) {
                logwarn("Switch the rig to Edit Rig mode to delete bones");
                return;
            }
            if (!RigDeleteBone(g_edit_item_index)) return;
            break;
        default:
            logerror("Deleting this element has not been supported yet");
            return;
    }
    DeselectEditTarget();
}

Panel GenerateEditPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Edit Selected");
    p.scrollable = TRUE;
	p.draw = DrawEditPanel;
	return p;
}
