/*
 * No audio device (PORT-PLAN.md Phase 4): the null build's half of host_audio.h. The sound driver still runs, so the
 * sound module opens and keeps its channels exactly as with a device; nothing is rendered.
 */
#include <platform/common/host_audio.h>

int J3DAudio_OpenDevice(J3DAudioRenderFunc pfRender, void* pUser, uint32_t* pSampleRate)
{
    (void)pfRender;
    (void)pUser;
    (void)pSampleRate;
    return 0;
}

void J3DAudio_CloseDevice(void)
{
}
