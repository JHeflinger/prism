#include "entity.h"
#include "game/definitions.h"
#include "game/ecs/components.h"
#include "game/ecs/registry.h"

void* EntityAddComponent(const Entity e, size_t type, void* component, size_t size) {
    return RegistryEmplaceComponent(e.context->registry, e.id, type, component, size);
}

void* EntityGetComponent(const Entity e, size_t type) {
    return RegistryGetComponent(e.context->registry, e.id, type);
}

BOOL EntityHasComponent(const Entity e, size_t type) {
    return RegistryHasComponent(e.context->registry, e.id, type);
}

void EntityRemoveComponent(const Entity e, size_t type) {
    EZ_ASSERT(RegistryHasComponent(e.context->registry, e.id, type), "Entity does not have this component!");
    RegistryRemoveComponent(e.context->registry, e.id, type);
}

Vector3* EntityPosition(const Entity e) {
    EZ_ASSERT(HasComponent(e, TransformComponent), "Entity does not have transform component!");
    TransformComponent* tc = GetComponent(e, TransformComponent);
    return &(tc->translation);
}

Vector3* EntityScale(const Entity e) {
    EZ_ASSERT(HasComponent(e, TransformComponent), "Entity does not have transform component!");
    TransformComponent* tc = GetComponent(e, TransformComponent);
    return &(tc->scale);
}

Vector3* EntityRotation(const Entity e) {
    EZ_ASSERT(HasComponent(e, TransformComponent), "Entity does not have transform component!");
    TransformComponent* tc = GetComponent(e, TransformComponent);
    return &(tc->rotation);
}

Vector3 GetWorldPosition(const Entity e) { // TODO: make these functions use parents
    EZ_ASSERT(HasComponent(e, TransformComponent), "Entity does not have transform component!");
    TransformComponent* tc = GetComponent(e, TransformComponent);
    return tc->translation;
}

Vector3 GetWorldScale(const Entity e) {
    EZ_ASSERT(HasComponent(e, TransformComponent), "Entity does not have transform component!");
    TransformComponent* tc = GetComponent(e, TransformComponent);
    return tc->scale;
}

Vector3 GetWorldRotation(const Entity e) {
    EZ_ASSERT(HasComponent(e, TransformComponent), "Entity does not have transform component!");
    TransformComponent* tc = GetComponent(e, TransformComponent);
    return tc->rotation;
}

Script* EntityScript(const Entity e) {
    if (HasComponent(e, ScriptComponent)) {
        GameScene* scene = e.context->parent;
        size_t scriptid = GetComponent(e, ScriptComponent)->id;
        if (scriptid == (size_t)-1) return NULL;
        EZ_ASSERT(scriptid < scene->assets.scripts.size, "Invalid script ID [%d] detected", (int)scriptid);
        return &(scene->assets.scripts.data[scriptid]);
    }
    return NULL;
}
