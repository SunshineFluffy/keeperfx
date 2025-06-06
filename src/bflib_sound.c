/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sound.c
 *     Sound and music related routines.
 * @par Purpose:
 *     Sound and music routines to use in games.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     16 Nov 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_sound.h"

#include <string.h>
#include <stdio.h>

#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_sndlib.h"
#include "bflib_fileio.h"
#include "bflib_planar.h"
#include "config_settings.h"
#include "game_legacy.h"
#include "globals.h"
#include "gui_soundmsgs.h"
#include "post_inc.h"

#define INVALID_SOUND_EMITTER (&emitter[0])

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Global variables
static long NoSoundEmitters = SOUND_EMITTERS_MAX;
struct SoundEmitter emitter[128];
static long MaxNoSounds;
static struct S3DSample SampleList[SOUNDS_MAX_COUNT];
static S3D_LineOfSight_Func LineOfSightFunction;
static long deadzone_radius;

TbBool SoundDisabled;
int atmos_sound_volume = 128;
long MaxSoundDistance;
struct SoundReceiver Receiver;
long Non3DEmitter;
long SpeechEmitter;
/******************************************************************************/
// Internal routines
SoundEmitterID allocate_free_sound_emitter(void);
void delete_sound_emitter(SoundEmitterID idx);
long start_emitter_playing(struct SoundEmitter *emit, SoundSmplTblID smptbl_id, SoundBankID bank_id, long smpitch, SoundVolume loudness, long fild1D, long ctype, unsigned char flags, long priority);
void init_sample_list(void);
void delete_all_sound_emitters(void);
SoundEmitterID get_emitter_id(struct SoundEmitter *emit);
long get_sample_id(struct S3DSample *sample);
void kick_out_sample(short smpl_id);
TbBool emitter_is_playing(struct SoundEmitter *emit);
TbBool remove_active_samples_from_emitter(struct SoundEmitter *emit);
/******************************************************************************/
// Functions

long dummy_line_of_sight_function(long a1, long a2, long a3, long a4, long a5, long a6)
{
    return 1;
}

long S3DInit(void)
{
    // Clear emitters memory
    delete_all_sound_emitters();
    // Reset sound receiver data
    memset(&Receiver, 0, sizeof(struct SoundReceiver));
    S3DSetSoundReceiverPosition(0, 0, 0);
    S3DSetSoundReceiverOrientation(0, 0, 0);
    S3DSetSoundReceiverSensitivity(64);
    S3DSetLineOfSightFunction(dummy_line_of_sight_function);
    S3DSetDeadzoneRadius(0);
    S3DSetNumberOfSounds(SOUNDS_MAX_COUNT);
    init_sample_list();
    return 1;
}

long S3DSetNumberOfSounds(long nMaxSounds)
{
    if (nMaxSounds > SOUNDS_MAX_COUNT)
        nMaxSounds = SOUNDS_MAX_COUNT;
    if (nMaxSounds < 1)
        nMaxSounds = 1;
    MaxNoSounds = nMaxSounds;
    return true;
}

struct SoundEmitter* S3DGetSoundEmitter(SoundEmitterID eidx)
{
    if ((eidx < 0) || (eidx >= SOUND_EMITTERS_MAX))
    {
        WARNLOG("Tried to get outranged emitter %ld",eidx);
        return INVALID_SOUND_EMITTER;
    }
    return &emitter[eidx];
}

TbBool S3DSoundEmitterInvalid(struct SoundEmitter *emit)
{
    if (emit == NULL)
        return true;
    if (emit == INVALID_SOUND_EMITTER)
        return true;
    return false;
}

TbBool S3DEmitterIsPlayingSample(SoundEmitterID eidx, SoundSmplTblID smpl_idx, SoundBankID bank_id)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return false;
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            if ((sample->smptbl_id == smpl_idx) && (sample->bank_id == bank_id)) {
                return true;
            }
        }
    }
    return false;
}

TbBool S3DDeleteSampleFromEmitter(SoundEmitterID eidx, SoundSmplTblID smpl_idx, SoundBankID bank_id)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return false;
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            if ((sample->smptbl_id == smpl_idx) && (sample->bank_id == bank_id)) {
                sample->is_playing = 0;
                stop_sample(get_emitter_id(emit), sample->smptbl_id, sample->bank_id);
                return true;
            }
        }
    }
    return false;
}

TbBool S3DDeleteAllSamplesFromEmitter(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit)) {
        ERRORLOG("Trying to delete samples from invalid emitter %ld",eidx);
        return false;
    }
    return stop_emitter_samples(emit);
}

long S3DSetMaximumSoundDistance(long nDistance)
{
    if (nDistance > 65536)
        nDistance = 65536;
    if (nDistance < 1)
        nDistance = 1;
    MaxSoundDistance = nDistance;
    return 1;
}

long S3DSetSoundReceiverPosition(int pos_x, int pos_y, int pos_z)
{
    Receiver.pos.val_x = pos_x;
    Receiver.pos.val_y = pos_y;
    Receiver.pos.val_z = pos_z;
    return 1;
}

long S3DSetSoundReceiverOrientation(int ori_a, int ori_b, int ori_c)
{
    Receiver.orient_a = ori_a & LbFPMath_AngleMask;
    Receiver.orient_b = ori_b & LbFPMath_AngleMask;
    Receiver.orient_c = ori_c & LbFPMath_AngleMask;
    return 1;
}

void S3DSetSoundReceiverFlags(unsigned long nflags)
{
    Receiver.flags = nflags;
}

void S3DSetSoundReceiverSensitivity(unsigned short nsensivity)
{
    Receiver.sensivity = nsensivity;
}

/**
 * Destroys sound emitter, without stopping samples which are being played.
 * @param eidx Sound emitter id.
 * @return True if emitter was destroyed, false if it already was.
 */
long S3DDestroySoundEmitter(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit)) {
        ERRORLOG("Invalid emitter %ld",eidx);
        return false;
    }
    remove_active_samples_from_emitter(emit);
    delete_sound_emitter(eidx);
    return true;
}

/**
 * Destroys sound emitter, stopping all samples which are being played.
 * @param eidx Sound emitter id.
 * @return True if emitter was destroyed, false if it already was.
 */
TbBool S3DDestroySoundEmitterAndSamples(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit)) {
        ERRORLOG("Invalid emitter %ld",eidx);
        return false;
    }
    stop_emitter_samples(emit);
    delete_sound_emitter(eidx);
    return true;
}

TbBool S3DEmitterIsAllocated(SoundEmitterID eidx)
{
    SYNCDBG(17,"Starting");
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return false;
    return ((emit->flags & Emi_IsAllocated) != 0);
}

TbBool S3DEmitterHasFinishedPlaying(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return true;
    return ((emit->flags & Emi_UnknownPlay) == 0);
}

TbBool S3DMoveSoundEmitterTo(SoundEmitterID eidx, long x, long y, long z)
{
    if (!S3DEmitterIsAllocated(eidx))
        return false;
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    emit->pos.val_x = x;
    emit->pos.val_y = y;
    emit->pos.val_z = z;
    emit->flags |= Emi_IsMoving;
    return true;
}

TbBool S3DAddSampleToEmitterPri(SoundEmitterID eidx, SoundSmplTblID smptbl_id, SoundBankID bank_id, SoundPitch pitch, SoundVolume loudness, long repeats, char ctype, long flags, long priority)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    return start_emitter_playing(emit, smptbl_id, bank_id, pitch, loudness, repeats, ctype, flags, priority) != 0;
}

long S3DCreateSoundEmitterPri(long x, long y, long z, SoundSmplTblID smptbl_id, SoundBankID bank_id, SoundPitch pitch, SoundVolume loudness, long repeats, long flags, long priority)
{
    long eidx = allocate_free_sound_emitter();
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return 0;
    emit->pos.val_x = x;
    emit->pos.val_y = y;
    emit->pos.val_z = z;
    emit->field_1 = flags;
    emit->curr_pitch = 100;
    emit->target_pitch = 100;
    if (start_emitter_playing(emit, smptbl_id, bank_id, pitch, loudness, repeats, 3, flags, priority))
        return eidx;
    delete_sound_emitter(eidx);
    return 0;
}

TbBool S3DEmitterIsPlayingAnySample(SoundEmitterID eidx)
{
    SYNCDBG(17,"Starting");
    if (MaxNoSounds <= 0)
        return false;
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
    {
        ERRORLOG("Invalid emiter %ld",eidx);
        return false;
    }
    TbBool is_playing = emitter_is_playing(emit);
    SYNCDBG(17,"Emitter %ld %s playing",eidx,is_playing?"is":"not");
    return is_playing;
}

void S3DSetLineOfSightFunction(S3D_LineOfSight_Func callback)
{
    LineOfSightFunction = callback;
}

void S3DSetDeadzoneRadius(long dzradius)
{
    deadzone_radius = dzradius;
}

long S3DGetDeadzoneRadius(void)
{
    return deadzone_radius;
}

SoundEmitterID get_emitter_id(struct SoundEmitter *emit)
{
    return (long)emit->index + 4000;
}

long get_sample_id(struct S3DSample *sample)
{
    return (long)sample->emit_idx + 4000;
}

short sound_emitter_in_use(SoundEmitterID eidx)
{
    return S3DEmitterIsAllocated(eidx);
}

long get_sound_distance(const struct SoundCoord3d *pos1, const struct SoundCoord3d *pos2)
{
    long dist_x = max(pos1->val_x, pos2->val_x) - min(pos1->val_x, pos2->val_x);
    long dist_y = max(pos1->val_y, pos2->val_y) - min(pos1->val_y, pos2->val_y);
    long dist_z = max(pos1->val_z, pos2->val_z) - min(pos1->val_z, pos2->val_z);
    // Make sure we're not exceeding sqrt(LONG_MAX/3), to fit the final result in long
    if (dist_x > 26754)
        dist_x = 26754;
    if (dist_y > 26754)
        dist_y = 26754;
    if (dist_z > 26754)
        dist_z = 26754;
    return LbSqrL( dist_y*dist_y + dist_x*dist_x + dist_z*dist_z );
}

long get_sound_squareedge_distance(const struct SoundCoord3d *pos1, const struct SoundCoord3d *pos2)
{
    long dist_x = max(pos1->val_x, pos2->val_x) - min(pos1->val_x, pos2->val_x);
    long dist_y = max(pos1->val_y, pos2->val_y) - min(pos1->val_y, pos2->val_y);
    long dist_z = max(pos1->val_z, pos2->val_z) - min(pos1->val_z, pos2->val_z);
    // Make sure we're not exceeding LONG_MAX/3
    if (dist_x > LONG_MAX/3)
        dist_x = LONG_MAX/3;
    if (dist_y > LONG_MAX/3)
        dist_y = LONG_MAX/3;
    if (dist_z > LONG_MAX/3)
        dist_z = LONG_MAX/3;
    return dist_x + dist_y + dist_z;
}

long get_emitter_distance(struct SoundReceiver *recv, struct SoundEmitter *emit)
{
    long dist = get_sound_distance(&recv->pos, &emit->pos);
    if (dist > MaxSoundDistance-1)
        dist = MaxSoundDistance-1;
    if (dist < 0)
        dist = 0;
    return dist;
}

long get_emitter_sight(struct SoundReceiver *recv, struct SoundEmitter *emit)
{
    return LineOfSightFunction(recv->pos.val_x, recv->pos.val_y, recv->pos.val_z, emit->pos.val_x, emit->pos.val_y, emit->pos.val_z);
}

long get_emitter_volume(const struct SoundReceiver *recv, const struct SoundEmitter *emit, long dist)
{
    long i = dist - deadzone_radius;
    if (i < 0) i = 0;
    long n = MaxSoundDistance - deadzone_radius;
    long long sens = recv->sensivity;
    long long vol = (127 - 127 * i / n) * sens;
    return (vol >> 6);
}

long get_emitter_pan(const struct SoundReceiver *recv, const struct SoundEmitter *emit)
{
    if ((recv->flags & Emi_IsAllocated) != 0) {
      return 64;
    }
    long diff_x = emit->pos.val_x - (long)recv->pos.val_x;
    long diff_y = emit->pos.val_y - (long)recv->pos.val_y;
    // Faster way of doing simple thing: radius = sqrt(dist_x*dist_y);
    long radius = LbDiagonalLength(abs(diff_x), abs(diff_y));
    if (radius < deadzone_radius) {
      return 64;
    }
    long angle_b = LbArcTanAngle(diff_x, diff_y);
    long angle_a = recv->orient_a;
    long angdiff = get_angle_difference(angle_a, angle_b);
    long angsign = get_angle_sign(angle_a, angle_b);
    long i = (radius - deadzone_radius) * LbSinL(angsign * angdiff) >> 16;
    long pan = (i << 6) / (MaxSoundDistance - deadzone_radius) + 64;
    if (pan > 127)
        pan = 127;
    if (pan < 0)
        pan = 0;
    return pan;
}

long get_emitter_pitch_from_doppler(const struct SoundReceiver *recv, struct SoundEmitter *emit)
{
    long target_pitch;
    long doppler_distance = get_sound_squareedge_distance(&emit->pos, &recv->pos);
    long delta = doppler_distance - emit->pitch_doppler;
    if (delta > 256)
        delta = 256;
    if (delta < 0)
        delta = 0;
    if (delta <= 0)
        target_pitch = 100;
    else
        target_pitch = 100 - 20 * delta / 256;
    long next_pitch = emit->curr_pitch;
    if (next_pitch != target_pitch)
    {
        next_pitch += (abs(target_pitch - next_pitch) >> 1);
    }
    emit->target_pitch = target_pitch;
    emit->curr_pitch = next_pitch;
    emit->pitch_doppler = doppler_distance;
    //texty += 16; // I have no idea what is this.. garbage.
    return emit->curr_pitch;
}

long get_emitter_pan_volume_pitch(struct SoundReceiver *recv, struct SoundEmitter *emit, long *pan, long *volume, long *pitch)
{
    TbBool on_sight;
    if ((emit->field_1 & 0x08) != 0)
    {
        *volume = 127;
        *pan = 64;
        *pitch = 100;
        return 1;
    }
    long dist = get_emitter_distance(recv, emit);
    if ((emit->field_1 & 0x04) != 0) {
        on_sight = 1;
    } else {
        on_sight = get_emitter_sight(recv, emit);
    }
    long i = get_emitter_volume(recv, emit, dist);
    if (on_sight) {
        *volume = i;
    } else {
        *volume = i >> 1;
    }
    i = (dist - deadzone_radius);
    if (i >= 128) {
        *pan = get_emitter_pan(recv, emit);
    } else {
        *pan = 64;
    }
    if ((emit->flags & Emi_IsMoving) != 0) {
        *pitch = get_emitter_pitch_from_doppler(recv, emit);
    } else {
        *pitch = 100;
    }
    //ERRORLOG("emit%2d expected %3d,%3d,%3d got %3d,%3d,%3d dist %3d",(int)emit->index ,opan, ovolume, opitch, *pan, *volume, *pitch, dist);
    return 1;
}

long set_emitter_pan_volume_pitch(struct SoundEmitter *emit, long pan, long volume, long pitch)
{
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            if ((sample->flags & Smp_Unknown02) == 0) {
              SetSampleVolume(get_emitter_id(emit), sample->smptbl_id, volume * (long)sample->base_volume / 256);
              SetSamplePan(get_emitter_id(emit), sample->smptbl_id, pan);
            }
            if ((sample->flags & Smp_Unknown01) == 0) {
              SetSamplePitch(get_emitter_id(emit), sample->smptbl_id, pitch * (long)sample->base_pitch / 100);
            }
        }
    }
    return 1;
}

TbBool process_sound_emitters(void)
{
    struct SoundEmitter *emit;
    long pan;
    long volume;
    long pitch;
    long i;
    for (i = 0; i < NoSoundEmitters; i++)
    {
        emit = S3DGetSoundEmitter(i);
        if ( ((emit->flags & Emi_IsAllocated) != 0) && ((emit->flags & Emi_UnknownPlay) != 0) )
        {
            if ( emitter_is_playing(emit) )
            {
                if (i == Non3DEmitter || i == SpeechEmitter) {
                    continue; // don't touch
                } else {
                    get_emitter_pan_volume_pitch(&Receiver, emit, &pan, &volume, &pitch);
                    set_emitter_pan_volume_pitch(emit, pan, volume, pitch);
                }
            } else
            {
                emit->flags ^= Emi_UnknownPlay;
            }
        }
    }
    return true;
}

TbBool emitter_is_playing(struct SoundEmitter *emit)
{
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            return true;
        }
    }
    return false;
}

TbBool remove_active_samples_from_emitter(struct SoundEmitter *emit)
{
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ( (sample->is_playing != 0) && (sample->emit_ptr == emit) )
        {
            if (sample->field_1D == -1)
            {
                stop_sample(get_emitter_id(emit), sample->smptbl_id, sample->bank_id);
                sample->is_playing = 0;
            }
            sample->emit_ptr = NULL;
        }
    }
    return true;
}

long stop_emitter_samples(struct SoundEmitter *emit)
{
    long num_stopped = 0;
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            stop_sample(get_emitter_id(emit), sample->smptbl_id, sample->bank_id);
            sample->is_playing = 0;
            num_stopped++;
        }
    }
    return num_stopped;
}

short find_slot(long fild8, SoundBankID bank_id, struct SoundEmitter *emit, long ctype, long spcmax)
{
    struct S3DSample *sample;
    long i;
    long spcval = 2147483647;
    short min_sample_id = SOUNDS_MAX_COUNT;
    if ((ctype == 2) || (ctype == 3))
    {
        for (i=0; i < MaxNoSounds; i++)
        {
            sample = &SampleList[i];
            if ( (sample->is_playing) && (sample->emit_ptr != NULL) )
            {
                if ( (sample->emit_ptr->index == emit->index)
                  && (sample->smptbl_id == fild8) && (sample->bank_id == bank_id) )
                    return i;
            }
        }
    }
    for (i=0; i < MaxNoSounds; i++)
    {
        sample = &SampleList[i];
        if (sample->is_playing == 0)
            return i;
        if (spcval > sample->priority)
        {
            min_sample_id = i;
            spcval = sample->priority;
        }
    }
    if (spcval >= spcmax)
    {
        return -1;
    }
    kick_out_sample(min_sample_id);
    return min_sample_id;
}

void play_non_3d_sample(SoundSmplTblID sample_idx)
{
    if (SoundDisabled)
        return;
    if (GetCurrentSoundMasterVolume() <= 0)
        return;
    if (Non3DEmitter != 0)
      if (!sound_emitter_in_use(Non3DEmitter))
      {
          ERRORLOG("Non 3d Emitter has been deleted!");
          Non3DEmitter = 0;
      }
    if (Non3DEmitter == 0)
    {
        Non3DEmitter = S3DCreateSoundEmitterPri(0, 0, 0, sample_idx, 0, 100, 256, 0, 8, 2147483646);
    } else
    {
        S3DAddSampleToEmitterPri(Non3DEmitter, sample_idx, 0, 100, 256, 0, 3, 8, 2147483646);
    }
}

void play_non_3d_sample_no_overlap(SoundSmplTblID smpl_idx)
{
    if (SoundDisabled)
        return;
    if (GetCurrentSoundMasterVolume() <= 0)
        return;
    if (Non3DEmitter != 0)
    {
        if (!sound_emitter_in_use(Non3DEmitter))
        {
            ERRORLOG("Non 3d Emitter has been deleted!");
            Non3DEmitter = 0;
        }
    }
    if (Non3DEmitter == 0)
    {
        Non3DEmitter = S3DCreateSoundEmitterPri(0, 0, 0, smpl_idx, 0, 100, 256, 0, 8, 0x7FFFFFFE);
    } else
    if (!S3DEmitterIsPlayingSample(Non3DEmitter, smpl_idx, 0))
    {
        S3DAddSampleToEmitterPri(Non3DEmitter, smpl_idx, 0, 100, 256, 0, 3, 8, 0x7FFFFFFE);
    }
}

void play_atmos_sound(SoundSmplTblID smpl_idx)
{
    if (SoundDisabled)
        return;
    if (GetCurrentSoundMasterVolume() <= 0)
        return;
    int ATMOS_SOUND_PITCH = (73 + (UNSYNC_RANDOM(10) * 6));
    // ATMOS0 has bigger range in pitch than other atmos sounds.
    if (smpl_idx == 1013)
    {
        ATMOS_SOUND_PITCH = (54 + (UNSYNC_RANDOM(16) * 4));
    }
    if (Non3DEmitter != 0)
    {
        if (!sound_emitter_in_use(Non3DEmitter))
        {
            ERRORLOG("Non 3d Emitter has been deleted!");
            Non3DEmitter = 0;
        }
    }
    if (Non3DEmitter == 0)
    {
        Non3DEmitter = S3DCreateSoundEmitterPri(0, 0, 0, smpl_idx, 0, ATMOS_SOUND_PITCH, atmos_sound_volume, 0, 8, 0x7FFFFFFE);
    } else
    if (!S3DEmitterIsPlayingSample(Non3DEmitter, smpl_idx, 0))
    {
        S3DAddSampleToEmitterPri(Non3DEmitter, smpl_idx, 0, ATMOS_SOUND_PITCH, atmos_sound_volume, 0, 3, 8, 0x7FFFFFFE);
        SYNCDBG(9,"Playing atmos sound %d with pitch %d",(int)smpl_idx,(int)ATMOS_SOUND_PITCH);
    }
}

/**
 * Initializes and returns sound emitter structure.
 * Returns its index; if no free emitter is found, returns 0.
 */
SoundEmitterID allocate_free_sound_emitter(void)
{
    for (long i = 1; i < NoSoundEmitters; i++)
    {
        if (!S3DEmitterIsAllocated(i))
        {
            struct SoundEmitter* emit = S3DGetSoundEmitter(i);
            emit->flags = Emi_IsAllocated;
            emit->index = i;
            return i;
        }
    }
    return 0;
}

/**
 * Clears sound emitter structure and marks it as unused.
 */
void delete_sound_emitter(SoundEmitterID idx)
{
    if (S3DEmitterIsAllocated(idx))
    {
        struct SoundEmitter* emit = S3DGetSoundEmitter(idx);
        memset(emit, 0, sizeof(struct SoundEmitter));
    }
}

/**
 * Drastic emitter clearing. Resets memory of all emitters, even unallocated ones.
 * Does not stop any playing samples - these should be cleared before this call.
 */
void delete_all_sound_emitters(void)
{
    for (long i = 0; i < SOUND_EMITTERS_MAX; i++)
    {
        struct SoundEmitter* emit = &emitter[i];
        memset(emit, 0, sizeof(struct SoundEmitter));
    }
}

void init_sample_list(void)
{
    for (long i = 0; i < SOUNDS_MAX_COUNT; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        memset(sample, 0, sizeof(struct S3DSample));
    }
}

void increment_sample_times(void)
{
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        sample->time_turn++;
    }
}

void kick_out_sample(short smpl_id)
{
    struct S3DSample* sample = &SampleList[smpl_id];
    stop_sample(get_sample_id(sample), sample->smptbl_id, sample->bank_id);
    sample->is_playing = 0;
}

TbBool process_sound_samples(void)
{
    for (long i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if (sample->is_playing != 0)
        {
            if (sample->mss_id <= 0)
            {
                ERRORLOG("Attempt to query invalid sample");
                continue;
            }
            if (!IsSamplePlaying(sample->mss_id))
            {
                sample->is_playing = 0;
            }
            if (sample->emit_ptr != NULL)
            {
              if ( (sample->volume == 0) ||
                 ( ((sample->emit_ptr->field_1 & 0x08) == 0) && (get_sound_distance(&sample->emit_ptr->pos, &Receiver.pos) > MaxSoundDistance) ) )
                kick_out_sample(i);
            }
        }
    }
    return true;
}

long speech_sample_playing(void)
{
    if (SoundDisabled) {
         SYNCDBG(7,"Disabled");
         return false;
     }
     if (GetCurrentSoundMasterVolume() <= 0) {
         SYNCDBG(17,"Volume zero");
         return false;
     }
     SYNCDBG(17,"Starting");
     if (Mix_Playing(MIX_SPEECH_CHANNEL))
     {
         return true;
     }
     long sp_emiter = SpeechEmitter;
     if (sp_emiter != 0)
     {
         if (S3DEmitterIsAllocated(SpeechEmitter))
         {
           sp_emiter = SpeechEmitter;
         } else
         {
           ERRORLOG("Speech Emitter has been deleted");
           sp_emiter = 0;
         }
     }
     SpeechEmitter = sp_emiter;
     if (sp_emiter == 0)
       return false;
     return S3DEmitterIsPlayingAnySample(sp_emiter);
}

long play_speech_sample(SoundSmplTblID smptbl_id)
{
    if (SoundDisabled)
      return false;
    if (GetCurrentSoundMasterVolume() <= 0)
      return false;
    long sp_emiter = SpeechEmitter;
    if (sp_emiter != 0)
    {
      if (S3DEmitterIsAllocated(SpeechEmitter))
      {
        sp_emiter = SpeechEmitter;
      } else
      {
        ERRORLOG("Speech Emitter has been deleted");
        sp_emiter = 0;
      }
    }
    SpeechEmitter = sp_emiter;
    long vol = LbLerp(0, 256, (float)settings.mentor_volume/127.0); // [0-127] rescaled to [0-256]

    if (sp_emiter != 0)
    {
      if (S3DEmitterHasFinishedPlaying(sp_emiter))
        if (S3DAddSampleToEmitterPri(SpeechEmitter, smptbl_id, 1, 100, vol, 0, 3, 8, 2147483647))
          return true;
      return false;
    }
    sp_emiter = S3DCreateSoundEmitterPri(0, 0, 0, smptbl_id, 1, 100, vol, 0, 8, 2147483647);
    SpeechEmitter = sp_emiter;
    if (sp_emiter == 0)
    {
      ERRORLOG("Cannot create speech emitter.");
      return false;
    }
    return true;
}

long start_emitter_playing(struct SoundEmitter *emit, SoundSmplTblID smptbl_id, SoundBankID bank_id, long smpitch, SoundVolume loudness, long fild1D, long ctype, unsigned char flags, long priority)
{
    long pan;
    long volume;
    long pitch;
    get_emitter_pan_volume_pitch(&Receiver, emit, &pan, &volume, &pitch);
    long smpl_idx = find_slot(smptbl_id, bank_id, emit, ctype, priority);
    volume = (volume * loudness) / 256;
    if (smpl_idx < 0)
        return 0;
    SoundMilesID mss_id = play_sample(get_emitter_id(emit), smptbl_id, volume, pan, smpitch, fild1D, ctype, bank_id);
    if (mss_id <= 0) {
        return 0;
    }
    struct S3DSample* sample = &SampleList[smpl_idx];
    sample->priority = priority;
    sample->smptbl_id = smptbl_id;
    sample->bank_id = bank_id;
    sample->emit_ptr = emit;
    sample->field_1D = fild1D;
    sample->volume = volume;
    sample->pan = pan;
    sample->base_pitch = smpitch;
    sample->is_playing = 1;
    sample->mss_id = mss_id;
    sample->flags = flags;
    sample->time_turn = 0;
    sample->emit_idx = emit->index;
    sample->sfxid = get_sample_sfxid(smptbl_id, bank_id);
    sample->base_volume = loudness;
    emit->flags |= Emi_UnknownPlay;
    return 1;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
