/*
 * Temporary debug input (PORT-PLAN.md P2.18): the keyboard half of stdControl, read straight from SDL's keyboard state,
 * so a level can be walked before Phase 3. It replaces the generated stdControl_keys_null.c in the apple build; the rest
 * of stdControl (mouse, joysticks, axes) stays null. Removed in P3.2, when stdControl_sdl.c replaces the whole module.
 *
 * Keys follow the retail model closely enough for the game's key bindings: a key's state is read once a frame
 * (stdControl_ReadControls), stdControl_ReadKey reports it held and counts a press on the frame it went down, and
 * stdControl_ReadKeyAsAxis reports 1 while held (retail reported the share of the frame it was held). Only the keys
 * in the table below exist: arrows, the modifiers, Space, Return, Escape, Tab, Backspace, the editing keys, letters,
 * digits and F1 to F12. Phase 3's DIK table (P3.1) covers the whole keyboard.
 *
 * Under SDL's dummy video driver (headless runs) no key is ever down, so runs stay deterministic.
 */
#include <string.h>

#include <SDL3/SDL.h>

#include <std/General/std.h>
#include <std/Win95/stdControl.h>

#include <Windows.h> // DIK_* (the shim's dinput subset)

typedef struct DebugKey
{
    SDL_Scancode scancode;
    uint8_t      dik;
} DebugKey;

static const DebugKey stdControl_aDebugKeys[] = {
    { SDL_SCANCODE_UP, DIK_UP }, { SDL_SCANCODE_DOWN, DIK_DOWN }, { SDL_SCANCODE_LEFT, DIK_LEFT },
    { SDL_SCANCODE_RIGHT, DIK_RIGHT }, { SDL_SCANCODE_LCTRL, DIK_LCONTROL }, { SDL_SCANCODE_RCTRL, DIK_RCONTROL },
    { SDL_SCANCODE_LSHIFT, DIK_LSHIFT }, { SDL_SCANCODE_RSHIFT, DIK_RSHIFT }, { SDL_SCANCODE_LALT, DIK_LMENU },
    { SDL_SCANCODE_RALT, DIK_RMENU }, { SDL_SCANCODE_SPACE, DIK_SPACE }, { SDL_SCANCODE_RETURN, DIK_RETURN },
    { SDL_SCANCODE_ESCAPE, DIK_ESCAPE }, { SDL_SCANCODE_TAB, DIK_TAB }, { SDL_SCANCODE_BACKSPACE, DIK_BACK },
    { SDL_SCANCODE_INSERT, DIK_INSERT }, { SDL_SCANCODE_DELETE, DIK_DELETE }, { SDL_SCANCODE_HOME, DIK_HOME },
    { SDL_SCANCODE_END, DIK_END }, { SDL_SCANCODE_PAGEUP, DIK_PRIOR }, { SDL_SCANCODE_PAGEDOWN, DIK_NEXT },
    { SDL_SCANCODE_A, DIK_A }, { SDL_SCANCODE_B, DIK_B }, { SDL_SCANCODE_C, DIK_C }, { SDL_SCANCODE_D, DIK_D },
    { SDL_SCANCODE_E, DIK_E }, { SDL_SCANCODE_F, DIK_F }, { SDL_SCANCODE_G, DIK_G }, { SDL_SCANCODE_H, DIK_H },
    { SDL_SCANCODE_I, DIK_I }, { SDL_SCANCODE_J, DIK_J }, { SDL_SCANCODE_K, DIK_K }, { SDL_SCANCODE_L, DIK_L },
    { SDL_SCANCODE_M, DIK_M }, { SDL_SCANCODE_N, DIK_N }, { SDL_SCANCODE_O, DIK_O }, { SDL_SCANCODE_P, DIK_P },
    { SDL_SCANCODE_Q, DIK_Q }, { SDL_SCANCODE_R, DIK_R }, { SDL_SCANCODE_S, DIK_S }, { SDL_SCANCODE_T, DIK_T },
    { SDL_SCANCODE_U, DIK_U }, { SDL_SCANCODE_V, DIK_V }, { SDL_SCANCODE_W, DIK_W }, { SDL_SCANCODE_X, DIK_X },
    { SDL_SCANCODE_Y, DIK_Y }, { SDL_SCANCODE_Z, DIK_Z }, { SDL_SCANCODE_1, DIK_1 }, { SDL_SCANCODE_2, DIK_2 },
    { SDL_SCANCODE_3, DIK_3 }, { SDL_SCANCODE_4, DIK_4 }, { SDL_SCANCODE_5, DIK_5 }, { SDL_SCANCODE_6, DIK_6 },
    { SDL_SCANCODE_7, DIK_7 }, { SDL_SCANCODE_8, DIK_8 }, { SDL_SCANCODE_9, DIK_9 }, { SDL_SCANCODE_0, DIK_0 },
    { SDL_SCANCODE_F1, DIK_F1 }, { SDL_SCANCODE_F2, DIK_F2 }, { SDL_SCANCODE_F3, DIK_F3 }, { SDL_SCANCODE_F4, DIK_F4 },
    { SDL_SCANCODE_F5, DIK_F5 }, { SDL_SCANCODE_F6, DIK_F6 }, { SDL_SCANCODE_F7, DIK_F7 }, { SDL_SCANCODE_F8, DIK_F8 },
    { SDL_SCANCODE_F9, DIK_F9 }, { SDL_SCANCODE_F10, DIK_F10 }, { SDL_SCANCODE_F11, DIK_F11 },
    { SDL_SCANCODE_F12, DIK_F12 },
};

static uint8_t stdControl_aKeyDown[STDCONTROL_MAX_KEYID];    // held this frame
static uint8_t stdControl_aKeyPressed[STDCONTROL_MAX_KEYID]; // went down this frame

int J3DAPI stdControl_Open(void)
{
    stdControl_bOpen = 1;
    stdControl_SetActivation(1);
    return 0;
}

void J3DAPI stdControl_Close(void)
{
    if ( !stdControl_bOpen )
    {
        return;
    }
    stdControl_SetActivation(0);
    stdControl_bOpen = 0;
}

void J3DAPI stdControl_Reset(void)
{
    // Retail switched mouse and joystick reading off and disabled every axis; none of those exist here yet.
}

void J3DAPI stdControl_ReadControls(void)
{
    if ( !stdControl_bOpen || !stdControl_bControlsActive )
    {
        return;
    }

    stdControl_bControlsIdle = 1;
    memset(stdControl_aKeyPressed, 0, sizeof(stdControl_aKeyPressed));

    const bool* pState = SDL_GetKeyboardState(NULL);
    for ( size_t i = 0; i < SDL_arraysize(stdControl_aDebugKeys); ++i )
    {
        const uint8_t dik  = stdControl_aDebugKeys[i].dik;
        const uint8_t down = pState && pState[stdControl_aDebugKeys[i].scancode] ? 1 : 0;
        stdControl_aKeyPressed[dik] = down && !stdControl_aKeyDown[dik];
        stdControl_aKeyDown[dik]    = down;
    }
}

void J3DAPI stdControl_FinishRead(void)
{
}

int J3DAPI stdControl_ReadKey(size_t keyNum, int* pbPressed)
{
    STD_ASSERTREL(keyNum < STDCONTROL_MAX_KEYID);
    if ( !stdControl_bControlsActive )
    {
        if ( pbPressed )
        {
            *pbPressed = 0;
        }
        return 0;
    }

    if ( pbPressed )
    {
        *pbPressed += stdControl_aKeyPressed[keyNum];
    }
    if ( stdControl_bControlsIdle && stdControl_aKeyDown[keyNum] )
    {
        stdControl_bControlsIdle = 0;
    }
    return stdControl_aKeyDown[keyNum];
}

float J3DAPI stdControl_ReadKeyAsAxis(size_t keyId)
{
    STD_ASSERTREL(keyId < STDCONTROL_MAX_KEYID);
    if ( !stdControl_bControlsActive )
    {
        return 0.0f;
    }
    if ( stdControl_aKeyDown[keyId] )
    {
        stdControl_bControlsIdle = 0;
        return 1.0f;
    }
    return 0.0f;
}

int J3DAPI stdControl_ControlsActive(void)
{
    return stdControl_bControlsActive;
}

// Retail released every key and (de)acquired the DirectInput devices. Returns 0, as retail did once it had.
int J3DAPI stdControl_SetActivation(int bActive)
{
    if ( !stdControl_bOpen )
    {
        return 1;
    }
    memset(stdControl_aKeyDown, 0, sizeof(stdControl_aKeyDown));
    memset(stdControl_aKeyPressed, 0, sizeof(stdControl_aKeyPressed));
    stdControl_bControlsActive = bActive;
    return 0;
}

int J3DAPI stdControl_ControlsIdle(void)
{
    return stdControl_bControlsIdle;
}
