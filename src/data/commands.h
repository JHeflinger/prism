#ifndef COMMANDS_H
#define COMMANDS_H

#include <easybool.h>

BOOL ConfigureCommand(char** argv, int argc);

BOOL ListCommand(char** argv, int argc);

BOOL HelpCommand(char** argv, int argc);

BOOL ClearCommand(char** argv, int argc);

BOOL NotifyCommand(char** argv, int argc);

BOOL RefreshCommand(char** argv, int argc);

BOOL TimeEventCommand(char** argv, int argc);

BOOL SelectCommand(char** argv, int argc);

#endif
