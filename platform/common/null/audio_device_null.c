/*
 * No audio device (PORT-PLAN.md Phase 4): the null build's half of host_audio.h. The sound driver still runs, so the
 * sound module opens and keeps its channels exactly as with a device; nothing is rendered or placed.
 */
#include <platform/common/host_audio.h>

int J3DAudio_OpenDevice(J3DAudioRenderFunc pfRender, J3DAudioRenderSlotFunc pfRenderSlot, void* pUser, uint32_t* pSampleRate)
{
    (void)pfRender;
    (void)pfRenderSlot;
    (void)pUser;
    (void)pSampleRate;
    return 0;
}

void J3DAudio_CloseDevice(void)
{
}

void J3DAudio_SetListener(const float aPos[3], const float aFront[3], const float aUp[3])
{
    (void)aPos;
    (void)aFront;
    (void)aUp;
}

void J3DAudio_SetSlotPosition(int slot, const float aPos[3])
{
    (void)slot;
    (void)aPos;
}

void J3DAudio_SetMirror(int bMirror)
{
    (void)bMirror;
}

int J3DAudio_HeadTrackingAvailable(void)
{
    return 0;
}

void J3DAudio_SetHeadTracking(int bEnabled)
{
    (void)bEnabled;
}
