#include "binds.h"
#include "ui/ui.h"
#include <easymemory.h>

BindNode g_root_bind = { 0 };

BOOL IsActionEndpoint(BindAction action) {
    return !(action == BIND_KEY_DOWN || action == BIND_BUTTON_DOWN);
}

BOOL IsActionKey(BindAction action) {
    return (action == BIND_KEY_DOWN || action == BIND_KEY_PRESSED || action == BIND_KEY_RELEASED || action == BIND_KEY_END);
}

void AddBindPath(BindNode** root, const char* name, BindFunc func, BindCommand command) {
    for (size_t i = 0; i < (*root)->nodes.size; i++) {
        BindCommand bc = ((BindNode*)((*root)->nodes.data[i]))->command;
        if (memcmp(&bc, &command, sizeof(BindCommand)) == 0) {
            EZ_ASSERT(!IsActionEndpoint(bc.action), "Duplicate bind detected - all binds must be unique");
            *root = ((BindNode*)((*root)->nodes.data[i]));
            return;
        }
    }
    BindNode* node = EZ_ALLOC(1, sizeof(BindNode));
    node->command = command;
    if (IsActionEndpoint(command.action)) {
        node->name = name;
        node->func = func;
    }
    ARRLIST_voidPtr_add(&((*root)->nodes), node);
    *root = node;
}

BindNode* GetBindSet(BindNode* node) {
    for (size_t i = 0; i < node->nodes.size; i++) {
        BindCommand bc = ((BindNode*)(node->nodes.data[i]))->command;
        if (!IsActionEndpoint(bc.action) &&
            ((bc.action == BIND_BUTTON_DOWN && InputButtonDown(bc.input)) ||
            (bc.action == BIND_KEY_DOWN && InputKeyDown(bc.input)))) {
            return GetBindSet((BindNode*)node->nodes.data[i]);
        }
    }
    return node;
}

void AddBind(const char* name, BindFunc func, ...) {
    va_list args;
    va_start(args, func);
    BindCommand curr;
    BindNode* node = &g_root_bind;
    do {
        curr = va_arg(args, BindCommand);
        AddBindPath(&node, name, func, curr);
    } while (!IsActionEndpoint(curr.action));
    va_end(args);
}

void ListenBinds() {
    BindNode* bindset = GetBindSet(&g_root_bind);
    for (size_t i = 0; i < bindset->nodes.size; i++) {
        BindNode* curr = (BindNode*)(bindset->nodes.data[i]);
        if (IsActionEndpoint(curr->command.action)) {
            switch(curr->command.action) {
                case BIND_BUTTON_PRESSED:
                    if (InputButtonPressed(curr->command.input)) curr->func();
                    break;
                case BIND_BUTTON_RELEASED:
                    if (InputButtonReleased(curr->command.input)) curr->func();
                    break;
                case BIND_BUTTON_END:
                    if (InputButtonDown(curr->command.input)) curr->func();
                    break;
                case BIND_KEY_PRESSED:
                    if (InputKeyPressed(curr->command.input)) curr->func();
                    break;
                case BIND_KEY_RELEASED:
                    if (InputKeyReleased(curr->command.input)) curr->func();
                    break;
                case BIND_KEY_END:
                    if (InputKeyDown(curr->command.input)) curr->func();
                    break;
                default:
                    EZ_ASSERT(FALSE, "Unhandled bind action detected");
                    break;
            }
        }
    }
}

void CleanBindNode(BindNode* node) {
    for (size_t i = 0; i < node->nodes.size; i++) {
        CleanBindNode((BindNode*)node->nodes.data[i]);
        EZ_FREE(node->nodes.data[i]);
    }
    ARRLIST_voidPtr_clear(&(node->nodes));
}

void CleanBinds() {
    CleanBindNode(&g_root_bind);
}

void DrawCurrentBinds(float x, float y) {
    UIMoveCursor(x, y);
    BindNode* bindset = GetBindSet(&g_root_bind);
    BOOL listed = FALSE;
    for (size_t i = 0; i < bindset->nodes.size; i++) {
        BindNode* curr = (BindNode*)(bindset->nodes.data[i]);
        if (IsActionEndpoint(curr->command.action)) {
            const char* identifier = IsActionKey(curr->command.action) ? InputKeyRepresentation(curr->command.input) : InputButtonRepresentation(curr->command.input);
            UIDrawSubtleText("[%s] %s", identifier, curr->name);
            listed = TRUE;
        }
    }
    BOOL first = TRUE;
    for (size_t i = 0; i < bindset->nodes.size; i++) {
        BindNode* curr = (BindNode*)(bindset->nodes.data[i]);
        if (!IsActionEndpoint(curr->command.action)) {
            if (first)  {
                if (listed) UIMoveCursor(0, 20);
                UIDrawSubtleText("Transition Keybinds:");
            }
            const char* identifier = IsActionKey(curr->command.action) ? InputKeyRepresentation(curr->command.input) : InputButtonRepresentation(curr->command.input);
            UIDrawSubtleText("<%s>", identifier);
            first = FALSE;
        }
    }
}
