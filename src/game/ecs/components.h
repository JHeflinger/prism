#ifndef COMPONENTS_H
#define COMPONENTS_H

#include "game/declarations.h"
#include <ui/ui.h>

#define EXPOSE_COMPONENT(name) enum { name##_TYPE = __COUNTER__ - CLEAN_COMPONENT_COUNTER - 1 }; struct name; typedef struct name name; struct name
#define START_COMPONENT_DEFINITIONS() enum { CLEAN_COMPONENT_COUNTER = __COUNTER__ } 
START_COMPONENT_DEFINITIONS();

typedef enum {
    TEXT_ALIGN_CENTER = 0,
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_RIGHT
} TextAlignment;

typedef enum {
    CENTER_ANCHOR = 0,
    ML_ANCHOR,
    MR_ANCHOR,
    TL_ANCHOR,
    TR_ANCHOR,
    BR_ANCHOR,
    BL_ANCHOR,
    TM_ANCHOR,
    BM_ANCHOR
} ViewportAnchor;

typedef enum {
    RECTANGLE_SHAPE = 0,
    CIRCLE_SHAPE
} ShapeType;

typedef enum {
    AUDIO_NOTHING = 0,
    AUDIO_PLAY,
    AUDIO_PAUSE,
    AUDIO_RESUME,
    AUDIO_STOP
} AudioCommand;

EXPOSE_COMPONENT(TagComponent) {
    const char* tag;
    BOOL metacollapsed;
};

EXPOSE_COMPONENT(ImageComponent) {
    size_t id;
    Color tint;
};

EXPOSE_COMPONENT(TransformComponent) {
    Vector3 translation;
    Vector3 rotation;
    Vector3 scale;
};

EXPOSE_COMPONENT(TextComponent) {
    char* text;
    size_t capacity;
    TextAlignment alignment;
    Color color;
    float size;
};

EXPOSE_COMPONENT(Camera2DComponent) {
    BOOL enabled;
    Vector2 offset;
    float rotation;
    float zoom;
};

EXPOSE_COMPONENT(Camera3DComponent) {

};

EXPOSE_COMPONENT(ShapeComponent) {
    ShapeType type;
    Color color;
};

EXPOSE_COMPONENT(ScriptComponent) {
    size_t id;
};

EXPOSE_COMPONENT(AnchorComponent) {
    ViewportAnchor anchor;
};

EXPOSE_COMPONENT(ParentComponent) {
    EntityID parentid;
};

EXPOSE_COMPONENT(ListenerComponent) {
    BOOL enabled;
    float volume;
    float decay;
};

EXPOSE_COMPONENT(SoundComponent) {
    size_t id;
    float volume;
    float pitch;
    float decay;
    AudioCommand command;
};

EXPOSE_COMPONENT(MusicComponent) {
    size_t id;
    float volume;
    float pitch;
    float decay;
    AudioCommand command;
};

EXPOSE_COMPONENT(EditUIComponent) {
    PanelFunction draw;
};

#endif
