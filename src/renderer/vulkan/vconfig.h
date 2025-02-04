#ifndef VCONFIG_H
#define VCONFIG_H

#define CPUSWAP_LENGTH 2
#define IMAGE_FORMAT VK_FORMAT_R8G8B8A8_SRGB
#define MAX_TEXTURES 32 // update in fragment shader too if changing this
#define INVOCATION_GROUP_SIZE 512

#ifdef PROD_BUILD
    #define ENABLE_VK_VALIDATION_LAYERS FALSE
#else
    #define ENABLE_VK_VALIDATION_LAYERS TRUE
#endif

#endif