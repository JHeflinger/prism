#include "commands.h"
#include <core/entrypoint.h>
#include <util/logger.h>
#include <core/config.h>
#include <easybasics.h>
#include <easyparse.h>
#include <stdlib.h>

DECLARE_ARRLIST_NAMED(charPtr, char*);
IMPL_ARRLIST_NAMED(charPtr, char*);

static ARRLIST_charPtr g_eventcommands = { 0 };
static ARRLIST_float g_eventtimes = { 0 };

static BlackBoardType InterpretType(const char* str) {
    if (strcmp(str, "TRUE") == 0) return BB_BOOL;
    if (strcmp(str, "FALSE") == 0) return BB_BOOL;
    if (strcmp(str, "True") == 0) return BB_BOOL;
    if (strcmp(str, "False") == 0) return BB_BOOL;
    if (strcmp(str, "true") == 0) return BB_BOOL;
    if (strcmp(str, "false") == 0) return BB_BOOL;
    if (strcmp(str, "NONE") == 0) return BB_LEVEL;
    if (strcmp(str, "TRACE") == 0) return BB_LEVEL;
    if (strcmp(str, "INFO") == 0) return BB_LEVEL;
    if (strcmp(str, "WARN") == 0) return BB_LEVEL;
    if (strcmp(str, "ERROR") == 0) return BB_LEVEL;
    BOOL numeric = TRUE;
    BOOL floaty = FALSE;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (i == 0 && str[i] == '-') continue;
        else if (str[i] < '0' || str[i] > '9') {
            if (str[i] != '.') {
                numeric = FALSE;
            } else {
                if (floaty) {
                    numeric = FALSE;
                } else {
                    floaty = TRUE;
                }
            }
        }
    }
    if (!numeric) return BB_STRING;
    if (floaty) return BB_FLOAT;
    return BB_INT;
}

static BOOL ParseAndSetValue(BlackBoardValue* value, const char* str) {
    switch (value->type) {
        case BB_STRING:
            strncpy(value->value._string, str, BLACKBOARD_STRING_SIZE);
            return TRUE;
        case BB_FLOAT:
            float f;
            if (ez_parse_float(str, &f)) {
                value->value._float = f;
                return TRUE;
            }
            return FALSE;
        case BB_INT:
            int64_t l;
            if (ez_parse_lint(str, &l)) {
                value->value._int = l;
                return TRUE;
            }
            return FALSE;
        case BB_BOOL:
            BOOL b;
            if (ez_parse_bool(str, &b)) {
                value->value._bool = b;
                return TRUE;
            }
            return FALSE;
        case BB_SIZE:
            size_t s;
            if (ez_parse_size(str, &s)) {
                value->value._size = s;
                return TRUE;
            }
            return FALSE;
        case BB_LEVEL:
            if (strcmp(str, "NONE") == 0) {
                value->value._level = LEVEL_NONE;
                return TRUE;
            }
            if (strcmp(str, "TRACE") == 0) {
                value->value._level = LEVEL_TRACE;
                return TRUE;
            }
            if (strcmp(str, "INFO") == 0) {
                value->value._level = LEVEL_INFO;
                return TRUE;
            }
            if (strcmp(str, "WARN") == 0) {
                value->value._level = LEVEL_WARN;
                return TRUE;
            }
            if (strcmp(str, "ERROR") == 0) {
                value->value._level = LEVEL_ERROR;
                return TRUE;
            }
            return FALSE;
        default: return FALSE;
    }
}

static const char* MessageLevelName(MessageLevel level) {
    switch (level) {
        case LEVEL_NONE:  return "NONE";
        case LEVEL_TRACE: return "TRACE";
        case LEVEL_INFO:  return "INFO";
        case LEVEL_WARN:  return "WARN";
        case LEVEL_ERROR: return "ERROR";
        default:          return "UNKNOWN";
    }
}

static BOOL LogConfigValue(const char* key, BlackBoardValue value) {
    switch (value.type) {
        case BB_INT:
            loginfo("\"%s\" = %d", key, value.value._int);
            return TRUE;
        case BB_FLOAT:
            loginfo("\"%s\" = %.6f", key, value.value._float);
            return TRUE;
        case BB_BOOL:
            loginfo("\"%s\" = %s", key, value.value._bool ? "TRUE" : "FALSE");
            return TRUE;
        case BB_SIZE:
            loginfo("\"%s\" = %llu", key, (unsigned long long)value.value._size);
            return TRUE;
        case BB_STRING:
            loginfo("\"%s\" = %s", key, value.value._string);
            return TRUE;
        case BB_LEVEL:
            loginfo("\"%s\" = %s", key, MessageLevelName(value.value._level));
            return TRUE;
        case BB_POINTER:
            loginfo("\"%s\" = <pointer %p>", key, value.value._ptr);
            return TRUE;
        case BB_CAMERA:
            loginfo("\"%s\" = <camera target=(%.2f, %.2f) offset=(%.2f, %.2f) rotation=%.2f zoom=%.2f>", key,
                value.value._camera.target.x, value.value._camera.target.y,
                value.value._camera.offset.x, value.value._camera.offset.y,
                value.value._camera.rotation, value.value._camera.zoom);
            return TRUE;
        default:
            logerror("\"%s\" has an unhandled datatype", key);
            return FALSE;
    }
}

typedef struct {
    const char** keys;
    size_t count;
    size_t capacity;
    const char* prefix;
} ConfigKeyCollector;

static void CollectConfigKey(const char* key, BlackBoardValue value, void* userdata) {
    (void)value;
    ConfigKeyCollector* collector = (ConfigKeyCollector*)userdata;
    if (collector->count >= collector->capacity) return;
    if (collector->prefix && strncmp(key, collector->prefix, strlen(collector->prefix)) != 0) return;
    collector->keys[collector->count++] = key;
}

static int CompareConfigKeys(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static void UpdateTimeEvents() {
    for (size_t i = 0; i < g_eventcommands.size; i++) {
        g_eventtimes.data[i] -= GetFrameTime();
        if (g_eventtimes.data[i] <= 0.0f) {
            ExecuteCommand(g_eventcommands.data[i]);
            EZ_FREE(g_eventcommands.data[i]);
            ARRLIST_charPtr_remove(&g_eventcommands, i);
            ARRLIST_float_remove(&g_eventtimes, i);
            i--;
        }
    }
} REGISTER_PREUPDATE(UpdateTimeEvents);

static void CleanTimeEvents() {
    for (size_t i = 0; i < g_eventcommands.size; i++) EZ_FREE(g_eventcommands.data[i]);
    ARRLIST_charPtr_clear(&g_eventcommands);
    ARRLIST_float_clear(&g_eventtimes);
} REGISTER_CLEANUP(CleanTimeEvents);

BOOL ConfigureCommand(char** argv, int argc) {
    if (argc < 2) {
        logerror("At least 2 arguments expected - config <read/write> <...>");
        return FALSE;
    }
    if (strcmp(argv[0], "read") == 0) {
        if (argc != 2) {
            logerror("Exactly 2 arguments expected - ocnfig read <parameter>");
            return FALSE;
        }
        if (!ConfigHas(argv[1])) {
            logerror("\"%s\" is not an existing configuration property", argv[1]);
            return FALSE;
        } else {
            return LogConfigValue(argv[1], ConfigGet(argv[1]));
        }
    } else if (strcmp(argv[0], "write") == 0) {
        if (argc != 3) {
            logerror("Exactly 3 arguments expected - ocnfig write <parameter> <value>");
            return FALSE;
        }
        if (ConfigHas(argv[1])) {
            BlackBoardValue value = ConfigGet(argv[1]);
            if (ParseAndSetValue(&value, argv[2])) {
                ConfigSet(argv[1], value);
                loginfo("Set \"%s\" to \"%s\" successfully", argv[1], argv[2]);
                return TRUE;
            } else {
                logerror("Unable to parse \"%s\" into a valid datatype", argv[2]);
                return FALSE;
            }
        } else {
            BlackBoardValue value = { 0 };
            value.type = InterpretType(argv[2]);
            if (ParseAndSetValue(&value, argv[2])) {
                ConfigSet(argv[1], value);
                loginfo("Set \"%s\" to \"%s\" successfully", argv[1], argv[2]);
                return TRUE;
            } else {
                logerror("Unable to parse \"%s\" into a valid datatype", argv[2]);
                return FALSE;
            }
        }
    } else {
        logerror("Unknown second argument \"%s\" detected - expected <read/write>", argv[0]);
        return FALSE;
    }
}

BOOL ListCommand(char** argv, int argc) {
    if (argc > 1) {
        logerror("At most 1 argument expected - list [prefix]");
        return FALSE;
    }
    size_t total = ConfigCount();
    if (total == 0) {
        loginfo("No configuration properties are currently set");
        return TRUE;
    }
    ConfigKeyCollector collector = { 0 };
    collector.keys = EZ_ALLOC(total, sizeof(const char*));
    collector.capacity = total;
    collector.prefix = argc == 1 ? argv[0] : NULL;
    ConfigForEach(CollectConfigKey, &collector);
    if (collector.count == 0) {
        loginfo("No configuration properties found matching prefix \"%s\"", collector.prefix);
        EZ_FREE(collector.keys);
        return TRUE;
    }
    qsort(collector.keys, collector.count, sizeof(const char*), CompareConfigKeys);
    if (collector.prefix) loginfo("%llu configuration properties matching \"%s\":", (unsigned long long)collector.count, collector.prefix);
    else loginfo("%llu configuration properties:", (unsigned long long)collector.count);
    BOOL success = TRUE;
    for (size_t i = 0; i < collector.count; i++) {
        if (!LogConfigValue(collector.keys[i], ConfigGet(collector.keys[i]))) success = FALSE;
    }
    EZ_FREE(collector.keys);
    return success;
}

BOOL HelpCommand(char** argv, int argc) {
    if (argc == 0) {
        loginfo("You can use this command with either the \"usage\" or \"descriptions\" arguments to print out the respective usage or descriptions of all available commands.");
        return TRUE;
    }
    if (argc == 1) {
        if (strcmp(argv[0], "usage") == 0) {
            ARRLIST_Command commands = GetCommands();
            for (size_t i = 0; i < commands.size; i++) loginfo("%d. %s", i + 1, commands.data[i].usage);
        } else if (strcmp(argv[0], "descriptions") == 0) {
            ARRLIST_Command commands = GetCommands();
            for (size_t i = 0; i < commands.size; i++) loginfo("%d. %s : %s", i + 1, commands.data[i].phrase, commands.data[i].description);
        } else {
            logerror("Invalid usage detected. Correct usage - help <usage|descriptions>");
            return FALSE;
        }
    } else {
        logerror("Invalid usage detected. Correct usage - help <usage|descriptions>");
        return FALSE;
    }
    return TRUE;
}

BOOL ClearCommand(char** argv, int argc) {
    if (argc == 0) {
        loginfo("No data subject targeted to clear");
        return TRUE;
    }
    if (argc == 1) {
        if (strcmp(argv[0], "notifications") == 0) {
            CleanNotifications();
        } else {
            logerror("Invalid data subject \"%s\" detected", argv[0]);
            return FALSE;
        }
    } else {
        logerror("More than one data subject targeted to clear detected");
        return FALSE;
    }
    return TRUE;
}

BOOL NotifyCommand(char** argv, int argc) {
    if (argc < 2) {
        logerror("Not enough arguments detected. Usage - notify <level> ...");
        return FALSE;
    }
    MessageLevel ml;
    if (!ParseMessageLevel(argv[0], &ml)) {
        logerror("Unable to parse message level \"%s\"", argv[0]);
        return FALSE;
    }
    char* message = ez_reconstruct_command(&(argv[1]), argc - 1);
    Notify(ml, message);
    EZ_FREE(message);
    logtrace("Successfully triggered notification");
    return TRUE;
}

BOOL RefreshCommand(char** argv, int argc) {
    if (argc == 0) {
        loginfo("No data subject targeted to refresh");
        return TRUE;
    }
    if (argc == 1) {
        if (strcmp(argv[0], "assets") == 0) {
            /* TODO: finish this
            CleanNotifications();
            if (GetActiveScene()) {
                loginfo("Refreshing assets...");
                RefreshAssets(GetActiveScene());
                loginfo("Refreshed assets");
            } else {
                logerror("No active scene loaded");
            }*/
            logerror("This command has not been implemented yet");
        } else {
            logerror("Invalid data subject \"%s\" detected", argv[0]);
            return FALSE;
        }
    } else {
        logerror("More than one data subject targeted to refresh detected");
        return FALSE;
    }
    return TRUE;
}

BOOL TimeEventCommand(char** argv, int argc) {
    if (argc < 2) {
        logerror("Not enough arguments specified - there must be at least 1 float and 1 string command!");
        return FALSE;
    }
    float time;
    if (!ez_parse_float(argv[0], &time)) {
        logerror("Unable to parse \"%s\" as a float", argv[0]);
        return FALSE;
    }
    ARRLIST_charPtr_add(&g_eventcommands, ez_reconstruct_command(&(argv[1]), argc - 1));
    ARRLIST_float_add(&g_eventtimes, time);
    logtrace("Registered command \"%s\" to run in %.3f seconds", g_eventcommands.data[g_eventcommands.size - 1], time);
    return TRUE;
}

BOOL SelectCommand(char** argv, int argc) {
    /*
    Scene* scene = GetActiveScene();
    if (!scene) {
        logerror("No active scene to select an entity from");
        return FALSE;
    }
    if (argc != 2) {
        logerror("This command requires 2 arguments - the world name and entity ID");
        return FALSE;
    }
    World* world = FindWorld(scene, argv[0]);
    if (!world) {
        logerror("No active world under the name \"%s\" detected - unable to select entity", argv[0]);
        return FALSE;
    }
    uint32_t id;
    if (!ez_parse_uint(argv[1], &id)) {
        logerror("Unable to parse \"%s\" as an ID", argv[1]);
        return FALSE;
    }
    Entity e = (Entity){ id, world };
    SelectEntity(e);
    loginfo("Successfully selected entity");*/
    logerror("This command has not been finished yet");
    return TRUE;
}
