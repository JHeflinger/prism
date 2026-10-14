#include "commands.h"
#include <util/logger.h>
#include <core/config.h>
#include <easyparse.h>

static BlackBoardType InterpretType(const char* str) {
    if (strcmp(str, "TRUE") == 0) return BB_BOOL;
    if (strcmp(str, "FALSE") == 0) return BB_BOOL;
    if (strcmp(str, "True") == 0) return BB_BOOL;
    if (strcmp(str, "False") == 0) return BB_BOOL;
    if (strcmp(str, "true") == 0) return BB_BOOL;
    if (strcmp(str, "false") == 0) return BB_BOOL;
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
        default: return FALSE;
    }
}

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
            BlackBoardValue value = ConfigGet(argv[1]);
            switch (value.type) {
                case BB_INT:
                    loginfo("\"%s\" = %d", argv[1], value.value._int);
                    break;
                case BB_FLOAT:
                    loginfo("\"%s\" = %.6f", argv[1], value.value._float);
                    break;
                case BB_BOOL:
                    loginfo("\"%s\" = %s", argv[1], value.value._bool ? "TRUE" : "FALSE");
                    break;
                case BB_SIZE:
                    loginfo("\"%s\" = %llu", argv[1], value.value._size);
                    break;
                case BB_STRING:
                    loginfo("\"%s\" = %s", argv[1], value.value._string);
                    break;
                default:
                    logerror("\"%s\" has an unhandled datatype", argv[1]);
                    return FALSE;
            }
            return TRUE;
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
