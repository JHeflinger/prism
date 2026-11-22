#include "audio.h"
#include "game/definitions.h"
#include "game/systems/system.h"
#include "game/ecs/entity.h"
#include "game/ecs/components.h"
#include "game/world.h"
#include <util/logger.h>
#include <raymath.h>

static void UpdateAudioSystem(GameSystem* system, float dt) {
    ARRLIST_EntityID* sounds = GetEntities(system->context, SoundComponent);
    ARRLIST_EntityID* musics = GetEntities(system->context, MusicComponent);
    ARRLIST_EntityID* listeners = GetEntities(system->context, ListenerComponent);
    ListenerComponent* listener = NULL;
    Vector3 lpos = { 0 };
    if (listeners) {
        int num = 0;
        for (size_t i = 0; i < listeners->size; i++) {
            Entity e = (Entity){ listeners->data[i], system->context };
            if (GetComponent(e, ListenerComponent)->enabled) {
                num++;
                listener = GetComponent(e, ListenerComponent);
                lpos = *(EntityPosition(e));
            }
        }
        if (num > 1) logwarn("%d active listener components detected", num);
    }
    if (sounds) {
        for (size_t i = 0; i < sounds->size; i++) {
            Entity e = (Entity){ sounds->data[i], system->context };
            SoundComponent* sc = GetComponent(e, SoundComponent);
            if (sc->id != (size_t)-1) {
                EZ_ASSERT(sc->id < system->context->parent->assets.sounds.size, "Invalid Sound ID [%d] detected", (int)sc->id);
                Sound sound = system->context->parent->assets.sounds.data[sc->id];
                switch (sc->command) {
                    case AUDIO_PLAY:
                        PlaySound(sound);
                        break;
                    case AUDIO_PAUSE:
                        PauseSound(sound);
                        break;
                    case AUDIO_STOP:
                        StopSound(sound);
                        break;
                    case AUDIO_RESUME:
                        ResumeSound(sound);
                        break;
                    default: break;
                }
                sc->command = AUDIO_NOTHING;
                SetSoundPitch(sound, sc->pitch);
                SetSoundPan(sound, 0.0f);
                if (listener) {
                    Vector3 spos = *(EntityPosition(e));
                    float distmetric = Vector3Length(Vector3Subtract(lpos, spos)) / 500.0f;
                    float volume = listener->volume * pow(1.0f - listener->decay, distmetric) * sc->volume * pow(1.0f - sc->decay, distmetric);
                    if (volume < 1.0f) SetSoundPan(sound, (1.0f - volume) * (spos.x < lpos.x ? 1.0f : -1.0f));
                    SetSoundVolume(sound, volume);
                } else {
                    SetSoundVolume(sound, sc->volume);
                }
            }
        }
    }
    if (musics) {
        for (size_t i = 0; i < musics->size; i++) {
            Entity e = (Entity){ musics->data[i], system->context };
            MusicComponent* mc = GetComponent(e, MusicComponent);
            if (mc->id != (size_t)-1) {
                EZ_ASSERT(mc->id < system->context->parent->assets.musics.size, "Invalid Music ID [%d] detected", (int)mc->id);
                Music music = system->context->parent->assets.musics.data[mc->id];
                switch (mc->command) {
                    case AUDIO_PLAY:
                        PlayMusicStream(music);
                        break;
                    case AUDIO_PAUSE:
                        PauseMusicStream(music);
                        break;
                    case AUDIO_STOP:
                        StopMusicStream(music);
                        break;
                    case AUDIO_RESUME:
                        ResumeMusicStream(music);
                        break;
                    default: break;
                }
                mc->command = AUDIO_NOTHING;
                SetMusicPitch(music, mc->pitch);
                SetMusicPan(music, 0.0f);
                if (listener) {
                    Vector3 spos = *(EntityPosition(e));
                    float distmetric = Vector3Length(Vector3Subtract(lpos, spos)) / 500.0f;
                    float volume = listener->volume * pow(1.0f - listener->decay, distmetric) * mc->volume * pow(1.0f - mc->decay, distmetric);
                    if (volume < 1.0f) SetMusicPan(music, (1.0f - volume) * (spos.x < lpos.x ? 1.0f : -1.0f));
                    SetMusicVolume(music, volume);
                } else {
                    SetMusicVolume(music, mc->volume);
                }
                UpdateMusicStream(music);
            }
        }
    }
}

static void CleanAudioSystem(GameSystem* system) {
    ARRLIST_EntityID* sounds = GetEntities(system->context, SoundComponent);
    ARRLIST_EntityID* musics = GetEntities(system->context, MusicComponent);
    if (sounds) {
        for (size_t i = 0; i < sounds->size; i++) {
            Entity e = (Entity){ sounds->data[i], system->context };
            size_t sid = GetComponent(e, SoundComponent)->id;
            if (sid != (size_t)-1) {
                EZ_ASSERT(sid < system->context->parent->assets.sounds.size, "Invalid Sound ID [%d] detected", (int)sid);
                StopSound(system->context->parent->assets.sounds.data[sid]);
            }
        }
    }
    if (musics) {
        for (size_t i = 0; i < musics->size; i++) {
            Entity e = (Entity){ musics->data[i], system->context };
            size_t mid = GetComponent(e, MusicComponent)->id;
            if (mid != (size_t)-1) {
                EZ_ASSERT(mid < system->context->parent->assets.musics.size, "Invalid Music ID [%d] detected", (int)mid);
                StopMusicStream(system->context->parent->assets.musics.data[mid]);
            }
        }
    }
}

GameSystem* GenerateAudioSystem() {
    return GenerateSystem(NULL, UpdateAudioSystem, NULL, NULL, NULL, NULL, CleanAudioSystem);
}
