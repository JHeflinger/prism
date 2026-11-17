#ifndef ANIMATION_H
#define ANIMATION_H

#include "renderer/rstructs.h"

BOOL PlayAnimations(Geometry* geometry);

float TimelineDT();

void OverrideTimelineDT(float dt);

#endif
