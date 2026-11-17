/*
 * stdControl over SDL3 (PORT-PLAN.md Phase 3): keyboard, mouse and controllers.
 *
 * Retail's stdControl.c read three DirectInput devices: the keyboard's state (GetDeviceState), the mouse's state for
 * its motion and its buffered events for its buttons, and each joystick's state. This file is that file with the
 * DirectInput calls replaced, and keeps its interface and its arithmetic: key ids, the pressed-this-frame count, the
 * share of the frame a key was held, axis registration, centre, scale and dead zone, the mouse's averaging and the
 * axis-as-key thresholds. What changed is only where the raw values come from:
 *
 *   keyboard     SDL's keyboard state, each key by position through the DIK table (host_keys.h, P3.1). Option is Alt
 *                and Command is the Windows key (P3.9).
 *   mouse        relative motion while the controls own the mouse (P3.4): SDL's relative mode, which hides and frees
 *                the cursor as DirectInput's exclusive foreground mode did. Motion is in points, scaled by the
 *                "Mouse Scale" setting (percent, default 100). Buttons and the wheel come as events from the event
 *                loop (stdControl_sdl.h), as DirectInput's buffered data did, so a click shorter than a frame counts.
 *   controllers  SDL gamepads, laid out as DirectInput showed an Xbox-style pad (P3.6): left stick X/Y, triggers on Z
 *                (left trigger positive), right stick Rx/Ry and also Rz (the "twist" axis the keysets can bind),
 *                buttons A B X Y LB RB Back Start LS RS Guide then the extras, the d-pad as hat 1. Other joysticks
 *                report their first six axes, 32 buttons and four hats as they are. Up to 8, in slots that keep their
 *                number while a controller is away; one connected into an empty slot is announced through
 *                stdControl_DevicesChanged so the game binds it.
 *
 * All of it is read into one snapshot per stdControl_ReadControls (ControlSnapshot), which is what a recording stores
 * and a replay supplies (host_record.h, P3.10), and what an input script writes into. The engine never sees a
 * difference between the three.
 */
#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <Windows.h>

#include <std/General/std.h>
#include <std/General/stdMath.h>
#include <std/Win95/stdControl.h>
#include <std/Win95/stdWin95.h>
#include <w32util/wuRegistry.h>

#include <platform/apple/stdControl_sdl.h>
#include <platform/common/host_keys.h>
#include <platform/common/host_record.h>

#define CONTROL_MAX_MOUSE_EVENTS 32u // retail's STDCONTROL_MOUSE_BUFFERSIZE
#define CONTROL_GAMEPAD_BUTTONS  17u // the buttons a standard gamepad reports, in the order below

// What one device read saw. Fixed size, every byte set, so a recording can compare and store it as bytes.
typedef struct ControlPad
{
    uint8_t  bPresent;
    uint8_t  bDualStick;
    uint8_t  numButtons;
    uint8_t  numPovs;
    int32_t  aAxes[STDCONTROL_JOYSTICK_NUMAXES];
    uint8_t  aButtons[STDCONTROL_NUM_JOYSTICK_BUTTONS];
    uint32_t aPovs[STDCONTROL_MAX_JOYSTICK_POVCONTROLERS]; // hundredths of a degree, 0xFFFFFFFF centred
} ControlPad;

typedef struct ControlMouseEvent
{
    uint8_t button; // 0 to 3
    uint8_t bDown;
} ControlMouseEvent;

typedef struct ControlSnapshot
{
    uint8_t           aKeys[STDCONTROL_MAX_KEYBOARD_BUTTONS]; // 0x80 when down, as GetDeviceState gave them
    int32_t           mouseDx;
    int32_t           mouseDy;
    int32_t           mouseDz;
    uint8_t           aMouseButtons[STDCONTROL_MAX_MOUSE_BUTTONS]; // the state after the events below
    uint32_t          numMouseEvents;
    ControlMouseEvent aMouseEvents[CONTROL_MAX_MOUSE_EVENTS];
    ControlPad        aPads[STDCONTROL_MAX_JOYSTICK_DEVICES];
} ControlSnapshot;

// The device each slot holds, live (not in a replay).
typedef struct ControlSlot
{
    SDL_JoystickID id;
    SDL_Gamepad*   pGamepad;
    SDL_Joystick*  pJoystick; // when it is not a gamepad
    int            bScripted; // the input script's controller
} ControlSlot;

static int   stdControl_bReadMouse                 = 1;
static float stdControl_updateKHz                  = 1.0f;
static float stdControl_updateHz                   = 1.0f;
static float stdControl_mouseXSensitivity          = 1.0f;
static float stdControl_mouseYSensitivity          = 1.0f;
static int   stdControl_cursorDisplayCounter       = 0;
static int   stdControl_aAxisStates[STDCONTROL_MAX_AXES];
static uint32_t stdControl_aKeyIdleTimes[STDCONTROL_MAX_KEYID];
static int   stdControl_aKeyInfo[STDCONTROL_MAX_KEYID];
StdControlAxis stdControl_aAxes[STDCONTROL_MAX_AXES];
static POINT stdControl_mousePos;
static int   stdControl_aKeyPressed[STDCONTROL_MAX_KEYID];

static int      stdControl_bStartup                 = 0;
int             stdControl_bOpen                    = 0;
int             stdControl_bControlsIdle            = 0;
int             stdControl_bControlsActive          = 0;
static int      stdControl_bMouseSensitivityEnabled = 0;
static int      stdControl_bReadJoysticks           = 0;
static size_t   stdControl_numJoystickDevices       = 0; // slots ever used: they keep their number
static uint32_t stdControl_curReadTime              = 0;
static uint32_t stdControl_lastReadTime             = 0;
static uint32_t stdControl_readDeltaTime            = 0;

// The port's state.
static ControlSnapshot control_snapshot;                         // the last read
static ControlPad      control_aAppliedPads[STDCONTROL_MAX_JOYSTICK_DEVICES]; // the device config the axes were set up for
static ControlSlot     control_aSlots[STDCONTROL_MAX_JOYSTICK_DEVICES];
static int             control_bDevicesChanged = 0;
static int             control_bWindowFocus    = 1;
static int             control_menuDepth       = 0; // menus up over the game window (J3DControl_SetMenuUp)
static int             control_bMouseCaptured  = 0;
static float           control_mouseScale      = 1.0f;
static float           control_mouseRemX       = 0.0f; // the fraction of a point not yet reported
static float           control_mouseRemY       = 0.0f;
static int32_t         control_wheel           = 0;    // WHEEL_DELTA units since the last read
static uint8_t         control_aMouseDown[STDCONTROL_MAX_MOUSE_BUTTONS];
static ControlMouseEvent control_aMouseEvents[CONTROL_MAX_MOUSE_EVENTS];
static uint32_t        control_numMouseEvents  = 0;

// The input script's devices (host_record.h).
static uint8_t    control_aScriptKeys[STDCONTROL_MAX_KEYBOARD_BUTTONS];
static uint8_t    control_aScriptMouseDown[STDCONTROL_MAX_MOUSE_BUTTONS];
static ControlPad control_scriptPad;

static void control_UpdateKeyState(size_t keyId, int bPressed, uint32_t tickTime);
static void control_EnableAxisRead(size_t axis);
static void control_ReadKeyboard(void);
static void control_ReadJoysticks(void);
static void control_ReadMouse(void);
static void control_UpdateMouseCapture(void);

/* ---- Devices ------------------------------------------------------------------------------------------------------ */

static int control_IsHeadless(void)
{
    const char* pValue = getenv("J3D_HEADLESS");
    return pValue && *pValue && *pValue != '0';
}

static int control_FindSlot(SDL_JoystickID id)
{
    for ( int i = 0; i < STDCONTROL_MAX_JOYSTICK_DEVICES; i++ )
    {
        if ( (control_aSlots[i].pGamepad || control_aSlots[i].pJoystick) && control_aSlots[i].id == id )
        {
            return i;
        }
    }
    return -1;
}

static void control_OpenDevice(SDL_JoystickID id)
{
    if ( control_FindSlot(id) >= 0 )
    {
        return; // SDL announces the devices present at startup too
    }

    int slot = -1;
    for ( int i = 0; i < STDCONTROL_MAX_JOYSTICK_DEVICES; i++ )
    {
        const ControlSlot* pSlot = &control_aSlots[i];
        if ( !pSlot->pGamepad && !pSlot->pJoystick && !pSlot->bScripted )
        {
            slot = i;
            break;
        }
    }
    if ( slot < 0 )
    {
        return; // retail's limit: 8 devices
    }

    ControlSlot* pSlot = &control_aSlots[slot];
    if ( SDL_IsGamepad(id) )
    {
        pSlot->pGamepad = SDL_OpenGamepad(id);
    }
    else
    {
        pSlot->pJoystick = SDL_OpenJoystick(id);
    }

    if ( pSlot->pGamepad || pSlot->pJoystick )
    {
        pSlot->id = id;
        const char* pName = pSlot->pGamepad ? SDL_GetGamepadName(pSlot->pGamepad) : SDL_GetJoystickName(pSlot->pJoystick);
        stdPrintf(std_pHS->pStatusPrint, J3D_FILE, __LINE__, "%s %d:%s\n", pSlot->pGamepad ? "Gamepad" : "Joystick",
                  slot + 1, pName ? pName : "", 0);
    }
}

static void control_CloseDevice(SDL_JoystickID id)
{
    const int slot = control_FindSlot(id);
    if ( slot < 0 )
    {
        return;
    }

    ControlSlot* pSlot = &control_aSlots[slot];
    if ( pSlot->pGamepad )
    {
        SDL_CloseGamepad(pSlot->pGamepad);
    }
    if ( pSlot->pJoystick )
    {
        SDL_CloseJoystick(pSlot->pJoystick);
    }
    memset(pSlot, 0, sizeof(*pSlot));
}

static uint32_t control_HatToPov(Uint8 hat)
{
    switch ( hat )
    {
        case SDL_HAT_UP:        return 0;
        case SDL_HAT_RIGHTUP:   return 4500;
        case SDL_HAT_RIGHT:     return 9000;
        case SDL_HAT_RIGHTDOWN: return 13500;
        case SDL_HAT_DOWN:      return 18000;
        case SDL_HAT_LEFTDOWN:  return 22500;
        case SDL_HAT_LEFT:      return 27000;
        case SDL_HAT_LEFTUP:    return 31500;
        default:                return 0xFFFFFFFFu;
    }
}

static int32_t control_Clamp16(int32_t value)
{
    return value < -32768 ? -32768 : (value > 32767 ? 32767 : value);
}

static void control_ReadGamepad(SDL_Gamepad* pGamepad, ControlPad* pPad)
{
    static const SDL_GamepadButton aButtons[CONTROL_GAMEPAD_BUTTONS] = {
        SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,          SDL_GAMEPAD_BUTTON_WEST,
        SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_BACK,          SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_LEFT_STICK,
        SDL_GAMEPAD_BUTTON_RIGHT_STICK,   SDL_GAMEPAD_BUTTON_GUIDE,         SDL_GAMEPAD_BUTTON_MISC1,
        SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,  SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
        SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,  SDL_GAMEPAD_BUTTON_TOUCHPAD,
    };

    pPad->bPresent   = 1;
    pPad->bDualStick = 1;
    pPad->numButtons = CONTROL_GAMEPAD_BUTTONS;
    pPad->numPovs    = 1;

    const int32_t leftTrigger  = SDL_GetGamepadAxis(pGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    const int32_t rightTrigger = SDL_GetGamepadAxis(pGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    const int32_t rightX       = SDL_GetGamepadAxis(pGamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    pPad->aAxes[STDCONTROL_JOYSTICK_AXIS_X]  = SDL_GetGamepadAxis(pGamepad, SDL_GAMEPAD_AXIS_LEFTX);
    pPad->aAxes[STDCONTROL_JOYSTICK_AXIS_Y]  = SDL_GetGamepadAxis(pGamepad, SDL_GAMEPAD_AXIS_LEFTY);
    pPad->aAxes[STDCONTROL_JOYSTICK_AXIS_Z]  = control_Clamp16(leftTrigger - rightTrigger);
    pPad->aAxes[STDCONTROL_JOYSTICK_AXIS_RX] = rightX;
    pPad->aAxes[STDCONTROL_JOYSTICK_AXIS_RY] = SDL_GetGamepadAxis(pGamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    pPad->aAxes[STDCONTROL_JOYSTICK_AXIS_RZ] = rightX;

    for ( size_t i = 0; i < CONTROL_GAMEPAD_BUTTONS; i++ )
    {
        pPad->aButtons[i] = SDL_GetGamepadButton(pGamepad, aButtons[i]) ? 0x80 : 0;
    }

    Uint8 hat = SDL_HAT_CENTERED;
    hat |= SDL_GetGamepadButton(pGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ? SDL_HAT_UP : 0;
    hat |= SDL_GetGamepadButton(pGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? SDL_HAT_RIGHT : 0;
    hat |= SDL_GetGamepadButton(pGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? SDL_HAT_DOWN : 0;
    hat |= SDL_GetGamepadButton(pGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? SDL_HAT_LEFT : 0;
    pPad->aPovs[0] = control_HatToPov(hat);
}

static void control_ReadJoystickDevice(SDL_Joystick* pJoystick, ControlPad* pPad)
{
    pPad->bPresent = 1;

    const int numAxes    = SDL_GetNumJoystickAxes(pJoystick);
    const int numButtons = SDL_GetNumJoystickButtons(pJoystick);
    const int numHats    = SDL_GetNumJoystickHats(pJoystick);
    pPad->numButtons     = (uint8_t)(numButtons < 0 ? 0 : (numButtons > STDCONTROL_NUM_JOYSTICK_BUTTONS ? STDCONTROL_NUM_JOYSTICK_BUTTONS : numButtons));
    pPad->numPovs        = (uint8_t)(numHats < 0 ? 0 : (numHats > STDCONTROL_MAX_JOYSTICK_POVCONTROLERS ? STDCONTROL_MAX_JOYSTICK_POVCONTROLERS : numHats));

    for ( int i = 0; i < STDCONTROL_JOYSTICK_NUMAXES && i < numAxes; i++ )
    {
        pPad->aAxes[i] = SDL_GetJoystickAxis(pJoystick, i);
    }
    for ( int i = 0; i < pPad->numButtons; i++ )
    {
        pPad->aButtons[i] = SDL_GetJoystickButton(pJoystick, i) ? 0x80 : 0;
    }
    for ( int i = 0; i < pPad->numPovs; i++ )
    {
        pPad->aPovs[i] = control_HatToPov(SDL_GetJoystickHat(pJoystick, i));
    }
}

static void control_ClearPad(ControlPad* pPad)
{
    memset(pPad, 0, sizeof(*pPad));
    for ( size_t i = 0; i < STDCONTROL_MAX_JOYSTICK_POVCONTROLERS; i++ )
    {
        pPad->aPovs[i] = 0xFFFFFFFFu;
    }
}

// The input script's events due by now, applied to its keyboard, mouse and controller (host_record.h).
static void control_ApplyScript(ControlSnapshot* pSnap)
{
    J3DScriptEvent event;
    while ( J3DRecord_TakeScriptEvent(0, &event) )
    {
        switch ( event.type )
        {
            case J3DSCRIPT_KEY:
                control_aScriptKeys[(uint8_t)event.a] = event.b ? 0x80 : 0;
                break;

            case J3DSCRIPT_MOUSE:
                pSnap->mouseDx += event.a;
                pSnap->mouseDy += event.b;
                pSnap->mouseDz += event.c;
                break;

            case J3DSCRIPT_BUTTON:
                if ( pSnap->numMouseEvents < CONTROL_MAX_MOUSE_EVENTS )
                {
                    pSnap->aMouseEvents[pSnap->numMouseEvents].button = (uint8_t)(event.a - 1);
                    pSnap->aMouseEvents[pSnap->numMouseEvents].bDown  = (uint8_t)event.b;
                    pSnap->numMouseEvents++;
                }
                control_aScriptMouseDown[event.a - 1] = event.b ? 0x80 : 0;
                break;

            case J3DSCRIPT_PAD:
                control_scriptPad.aButtons[event.a - 1] = event.b ? 0x80 : 0;
                break;

            case J3DSCRIPT_AXIS:
                control_scriptPad.aAxes[event.a] = event.b;
                break;

            case J3DSCRIPT_HAT:
            {
                static const Uint8 aHats[] = { SDL_HAT_CENTERED, SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
                control_scriptPad.aPovs[0] = control_HatToPov(aHats[event.a]);
                break;
            }

            default:
                break;
        }
    }

    for ( size_t i = 0; i < STDCONTROL_MAX_KEYBOARD_BUTTONS; i++ )
    {
        pSnap->aKeys[i] |= control_aScriptKeys[i];
    }
    for ( size_t i = 0; i < STDCONTROL_MAX_MOUSE_BUTTONS; i++ )
    {
        pSnap->aMouseButtons[i] |= control_aScriptMouseDown[i];
    }
}

// Fills *pSnap from the devices (and the input script), taking the buffered mouse events and motion.
static void control_ReadDevices(ControlSnapshot* pSnap)
{
    memset(pSnap, 0, sizeof(*pSnap));

    int numKeys = 0;
    const bool* pState = SDL_GetKeyboardState(&numKeys);
    for ( int hid = 0; pState && hid < numKeys; hid++ )
    {
        if ( pState[hid] )
        {
            const uint8_t dik = J3DHost_HidToDik((uint32_t)hid);
            if ( dik )
            {
                pSnap->aKeys[dik] = 0x80;
            }
        }
    }

    // Motion since the last read, in points, scaled; the remainder is carried to the next read.
    float dx = 0.0f;
    float dy = 0.0f;
    SDL_GetRelativeMouseState(&dx, &dy);
    if ( control_bMouseCaptured )
    {
        control_mouseRemX += dx * control_mouseScale;
        control_mouseRemY += dy * control_mouseScale;
        pSnap->mouseDx = (int32_t)control_mouseRemX;
        pSnap->mouseDy = (int32_t)control_mouseRemY;
        control_mouseRemX -= (float)pSnap->mouseDx;
        control_mouseRemY -= (float)pSnap->mouseDy;
        pSnap->mouseDz = control_wheel;
    }
    control_wheel = 0;

    memcpy(pSnap->aMouseButtons, control_aMouseDown, sizeof(pSnap->aMouseButtons));
    pSnap->numMouseEvents = control_numMouseEvents;
    memcpy(pSnap->aMouseEvents, control_aMouseEvents, sizeof(pSnap->aMouseEvents));
    control_numMouseEvents = 0;

    for ( size_t i = 0; i < STDCONTROL_MAX_JOYSTICK_DEVICES; i++ )
    {
        ControlPad* pPad = &pSnap->aPads[i];
        control_ClearPad(pPad);
        if ( control_aSlots[i].pGamepad )
        {
            control_ReadGamepad(control_aSlots[i].pGamepad, pPad);
        }
        else if ( control_aSlots[i].pJoystick )
        {
            control_ReadJoystickDevice(control_aSlots[i].pJoystick, pPad);
        }
    }

    if ( J3DRecord_HasScript() )
    {
        control_ApplyScript(pSnap);
        for ( size_t i = 0; i < STDCONTROL_MAX_JOYSTICK_DEVICES; i++ )
        {
            if ( control_aSlots[i].bScripted )
            {
                pSnap->aPads[i] = control_scriptPad;
            }
        }
    }
}

// Sets up the axes of slots whose device appeared or changed kind since the last snapshot, as retail's
// InitJoysticks did at startup (range, dead zone 0.2), and drops those of slots that emptied.
static void control_ApplyDevices(const ControlSnapshot* pSnap)
{
    for ( size_t joyNum = 0; joyNum < STDCONTROL_MAX_JOYSTICK_DEVICES; joyNum++ )
    {
        const ControlPad* pPad     = &pSnap->aPads[joyNum];
        ControlPad*       pApplied = &control_aAppliedPads[joyNum];
        if ( pPad->bPresent == pApplied->bPresent && pPad->bDualStick == pApplied->bDualStick
            && pPad->numButtons == pApplied->numButtons && pPad->numPovs == pApplied->numPovs )
        {
            continue;
        }

        if ( pPad->bPresent )
        {
            for ( size_t axis = 0; axis < STDCONTROL_JOYSTICK_NUMAXES; axis++ )
            {
                const size_t aid = STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axis);
                const int    bEnabled = (stdControl_aAxes[aid].flags & STDCONTROL_AXIS_ENABLED) != 0;
                stdControl_RegisterAxis(aid, -32768, 32767, 0.2f);
                if ( bEnabled )
                {
                    stdControl_aAxes[aid].flags |= STDCONTROL_AXIS_ENABLED;
                }
            }

            if ( joyNum >= stdControl_numJoystickDevices )
            {
                // A slot the game has never seen: its controls are not bound yet.
                stdControl_numJoystickDevices = joyNum + 1;
                if ( stdControl_bStartup )
                {
                    control_bDevicesChanged = 1;
                }
            }
        }
        else
        {
            // Away: its buttons are released (by the read that follows) and its axes read centred.
            for ( size_t axis = 0; axis < STDCONTROL_JOYSTICK_NUMAXES; axis++ )
            {
                stdControl_aAxisStates[STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axis)] = 0;
            }
        }

        pApplied->bPresent   = pPad->bPresent;
        pApplied->bDualStick = pPad->bDualStick;
        pApplied->numButtons = pPad->numButtons;
        pApplied->numPovs    = pPad->numPovs;
    }
}

/* ---- Mouse capture ------------------------------------------------------------------------------------------------ */

// The mouse is the game's (relative mode, no cursor) while the controls are active and read it and the window has
// focus, as DirectInput's exclusive foreground mouse was acquired. Never in a replay or headless.
static void control_UpdateMouseCapture(void)
{
    SDL_Window* pWindow = (SDL_Window*)stdWin95_GetWindow();
    const int bWant = stdControl_bOpen && stdControl_bControlsActive && stdControl_bReadMouse && control_bWindowFocus
        && control_menuDepth == 0 && pWindow && !J3DRecord_IsReplaying() && !control_IsHeadless();
    if ( bWant == control_bMouseCaptured )
    {
        return;
    }

    control_bMouseCaptured = bWant;
    if ( pWindow )
    {
        SDL_SetWindowRelativeMouseMode(pWindow, bWant ? true : false);
    }

    // Motion and clicks made while the mouse was free are not the game's.
    float dx;
    float dy;
    SDL_GetRelativeMouseState(&dx, &dy);
    control_mouseRemX      = 0.0f;
    control_mouseRemY      = 0.0f;
    control_wheel          = 0;
    control_numMouseEvents = 0;
}

void J3DControl_SetWindowFocus(int bFocus)
{
    control_bWindowFocus = bFocus;
    control_UpdateMouseCapture();
}

void J3DControl_SetMenuUp(int bUp)
{
    control_menuDepth += bUp ? 1 : -1;
    if ( control_menuDepth < 0 )
    {
        control_menuDepth = 0;
    }
    control_UpdateMouseCapture();
}

void J3DControl_HandleEvent(const SDL_Event* pEvent)
{
    if ( !stdControl_bStartup || J3DRecord_IsReplaying() )
    {
        return;
    }

    switch ( pEvent->type )
    {
        case SDL_EVENT_JOYSTICK_ADDED:
            control_OpenDevice(pEvent->jdevice.which);
            break;

        case SDL_EVENT_JOYSTICK_REMOVED:
            control_CloseDevice(pEvent->jdevice.which);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            int button;
            switch ( pEvent->button.button )
            {
                case SDL_BUTTON_LEFT:   button = 0; break;
                case SDL_BUTTON_RIGHT:  button = 1; break;
                case SDL_BUTTON_MIDDLE: button = 2; break;
                case SDL_BUTTON_X1:     button = 3; break;
                default:                return;
            }

            if ( !control_bMouseCaptured )
            {
                return; // a click that gives the window focus, or one on the cursor in a window, is not the game's
            }

            control_aMouseDown[button] = pEvent->button.down ? 0x80 : 0;
            if ( control_numMouseEvents < CONTROL_MAX_MOUSE_EVENTS )
            {
                control_aMouseEvents[control_numMouseEvents].button = (uint8_t)button;
                control_aMouseEvents[control_numMouseEvents].bDown  = pEvent->button.down ? 1 : 0;
                control_numMouseEvents++;
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
            if ( control_bMouseCaptured )
            {
                float y = pEvent->wheel.y;
                if ( pEvent->wheel.direction == SDL_MOUSEWHEEL_FLIPPED )
                {
                    y = -y;
                }
                control_wheel += (int32_t)(y * 120.0f); // WHEEL_DELTA per notch, as DirectInput reported lZ
            }
            break;

        default:
            break;
    }
}

/* ---- stdControl.h ------------------------------------------------------------------------------------------------- */

int J3DAPI stdControl_Startup(int bKeyboardForeground)
{
    (void)bKeyboardForeground; // the keyboard is read only while the window has focus, as with DISCL_FOREGROUND

    if ( stdControl_bStartup )
    {
        return 1;
    }

    memset(stdControl_aKeyIdleTimes, 0, sizeof(stdControl_aKeyIdleTimes));
    memset(stdControl_aKeyInfo, 0, sizeof(stdControl_aKeyInfo));
    memset(stdControl_aAxes, 0, sizeof(stdControl_aAxes));
    memset(stdControl_aAxisStates, 0, sizeof(stdControl_aAxisStates));
    memset(control_aSlots, 0, sizeof(control_aSlots));
    memset(control_aScriptKeys, 0, sizeof(control_aScriptKeys));
    memset(control_aScriptMouseDown, 0, sizeof(control_aScriptMouseDown));
    memset(control_aMouseDown, 0, sizeof(control_aMouseDown));
    stdControl_mousePos.x           = 0;
    stdControl_mousePos.y           = 0;
    stdControl_numJoystickDevices   = 0;
    stdControl_cursorDisplayCounter = 0;
    control_numMouseEvents          = 0;
    control_wheel                   = 0;
    control_bDevicesChanged         = 0;
    for ( size_t i = 0; i < STDCONTROL_MAX_JOYSTICK_DEVICES; i++ )
    {
        control_ClearPad(&control_aAppliedPads[i]);
    }

    const int scale = wuRegistry_GetIntEx("Mouse Scale", 100);
    control_mouseScale = (float)(scale > 0 ? scale : 100) / 100.0f;

    // The controllers present now take the first slots, as DirectInput's enumeration did.
    if ( !J3DRecord_IsReplaying() )
    {
        if ( J3DRecord_ScriptHasPad() )
        {
            control_aSlots[0].bScripted = 1;
            control_ClearPad(&control_scriptPad);
            control_scriptPad.bPresent   = 1;
            control_scriptPad.bDualStick = 1;
            control_scriptPad.numButtons = CONTROL_GAMEPAD_BUTTONS;
            control_scriptPad.numPovs    = 1;
        }
        else if ( !control_IsHeadless() )
        {
            if ( !SDL_InitSubSystem(SDL_INIT_GAMEPAD) )
            {
                stdPrintf(std_pHS->pErrorPrint, J3D_FILE, __LINE__, "SDL could not start controller support: %s.\n", SDL_GetError(), 0, 0, 0);
            }
            else
            {
                int numIds = 0;
                SDL_JoystickID* aIds = SDL_GetJoysticks(&numIds);
                for ( int i = 0; aIds && i < numIds; i++ )
                {
                    control_OpenDevice(aIds[i]);
                }
                SDL_free(aIds);
            }
        }
    }

    // The device configuration is the first thing a recording stores: the game binds its controls to it.
    control_ReadDevices(&control_snapshot);
    J3DRecord_Input(&control_snapshot, sizeof(control_snapshot));
    control_ApplyDevices(&control_snapshot);

    // Retail registered the mouse axes at startup (InitMouse).
    stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_X, -250, 250, 0.0f);
    stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_Y, -200, 200, 0.0f);
    stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_Z, -20, 20, 0.0f);

    stdControl_Reset();
    stdControl_bStartup = 1;
    return 0;
}

void J3DAPI stdControl_Shutdown(void)
{
    if ( !stdControl_bStartup )
    {
        return;
    }

    stdControl_bStartup = 0;
    for ( size_t i = 0; i < STDCONTROL_MAX_JOYSTICK_DEVICES; i++ )
    {
        if ( control_aSlots[i].pGamepad || control_aSlots[i].pJoystick )
        {
            control_CloseDevice(control_aSlots[i].id);
        }
    }
    memset(control_aSlots, 0, sizeof(control_aSlots));
    stdControl_numJoystickDevices = 0;

    control_bWindowFocus = 0;
    control_UpdateMouseCapture();
    control_bWindowFocus = 1;
}

int J3DAPI stdControl_Open(void)
{
    if ( !stdControl_bStartup )
    {
        return 1;
    }

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
    stdControl_bReadMouse               = 0;
    stdControl_bReadJoysticks           = 0;
    stdControl_bMouseSensitivityEnabled = 0;
    for ( size_t i = 0; i < STDCONTROL_MAX_AXES; i++ )
    {
        stdControl_aAxes[i].flags &= ~STDCONTROL_AXIS_ENABLED;
    }
    control_UpdateMouseCapture();
}

void J3DAPI stdControl_DisableReadJoysticks(void)
{
    stdControl_bReadJoysticks = 0;
}

int J3DAPI stdControl_EnableAxis(size_t axis)
{
    axis = STDCONTROL_GETAID(axis);
    if ( axis >= STDCONTROL_MAX_AXES )
    {
        return 0;
    }

    if ( (stdControl_aAxes[axis].flags & STDCONTROL_AXIS_REGISTERED) == 0 )
    {
        return 0;
    }

    stdControl_aAxes[axis].flags |= STDCONTROL_AXIS_ENABLED;
    control_EnableAxisRead(axis);
    return 1;
}

void J3DAPI stdControl_ReadControls(void)
{
    STD_ASSERTREL(stdControl_bStartup && stdControl_bOpen);
    if ( !stdControl_bControlsActive )
    {
        return;
    }

    stdControl_bControlsIdle = 1;
    memset(stdControl_aKeyIdleTimes, 0, sizeof(stdControl_aKeyIdleTimes));
    memset(stdControl_aKeyPressed, 0, sizeof(stdControl_aKeyPressed));

    stdControl_curReadTime   = timeGetTime();
    stdControl_readDeltaTime = stdControl_curReadTime - stdControl_lastReadTime;

    // Port: retail divided by the delta unguarded; two reads in the same millisecond gave infinity, which only the
    // share-of-frame arithmetic below reads. On the virtual clock (headless, replay) two reads in one frame are common.
    stdControl_updateKHz = stdControl_readDeltaTime ? 1.0f / (float)stdControl_readDeltaTime : 1.0f;
    stdControl_updateHz  = 1000.0f * stdControl_updateKHz;

    if ( stdControl_bMouseSensitivityEnabled )
    {
        // Retail cleared 7 bytes of the joystick axes here (memset's size in bytes, not entries); kept.
        memset(&stdControl_aAxisStates[0], 0, 7);
    }
    else
    {
        memset(stdControl_aAxisStates, 0, sizeof(stdControl_aAxisStates));
    }

    control_UpdateMouseCapture();
    if ( !J3DRecord_IsReplaying() )
    {
        control_ReadDevices(&control_snapshot);
    }
    J3DRecord_Input(&control_snapshot, sizeof(control_snapshot)); // stores it, or replaces it with the recorded one
    control_ApplyDevices(&control_snapshot);

    control_ReadKeyboard();
    if ( stdControl_bReadJoysticks )
    {
        control_ReadJoysticks();
    }
    control_ReadMouse();

    stdControl_lastReadTime = stdControl_curReadTime;
}

float J3DAPI stdControl_ReadAxis(size_t axis)
{
    axis = STDCONTROL_GETAID(axis);
    if ( axis >= STDCONTROL_MAX_AXES || !stdControl_bControlsActive || (stdControl_aAxes[axis].flags & STDCONTROL_AXIS_ENABLED) == 0 )
    {
        return 0.0f;
    }

    const int pos = stdControl_aAxisStates[axis] - stdControl_aAxes[axis].center;
    if ( !pos )
    {
        return 0.0f;
    }

    if ( (stdControl_aAxes[axis].flags & STDCONTROL_AXIS_HASDEADZONE) == 0 && stdControl_aAxes[axis].deadzoneThreshold
        && (pos < 0 ? -pos : pos) < stdControl_aAxes[axis].deadzoneThreshold )
    {
        return 0.0f;
    }

    float curvalue = (float)pos * stdControl_aAxes[axis].scale;
    curvalue       = (curvalue < 0.0f ? -curvalue : curvalue) > 0.00001f ? curvalue : 0.0f;
    if ( stdControl_bControlsIdle && curvalue != 0.0f )
    {
        stdControl_bControlsIdle = 0;
    }
    return curvalue;
}

int J3DAPI stdControl_ReadAxisRaw(size_t axis)
{
    axis = STDCONTROL_GETAID(axis);
    if ( axis >= STDCONTROL_MAX_AXES || !stdControl_bControlsActive || (stdControl_aAxes[axis].flags & STDCONTROL_AXIS_ENABLED) == 0 )
    {
        return 0;
    }

    const int pos = stdControl_aAxisStates[axis] - stdControl_aAxes[axis].center;
    if ( stdControl_bControlsIdle && pos )
    {
        stdControl_bControlsIdle = 0;
    }
    return pos;
}

float J3DAPI stdControl_ReadKeyAsAxis(size_t keyId)
{
    STD_ASSERTREL(keyId < STDCONTROL_MAX_KEYID);
    if ( !stdControl_bControlsActive )
    {
        return 0.0f;
    }

    uint32_t time = stdControl_aKeyIdleTimes[keyId];
    if ( !time )
    {
        if ( !stdControl_aKeyInfo[keyId] )
        {
            return 0.0f;
        }
        time = stdControl_readDeltaTime;
    }

    time = time < stdControl_readDeltaTime ? time : stdControl_readDeltaTime;
    const float deltaTime = (float)time * stdControl_updateKHz;
    if ( stdControl_bControlsIdle && deltaTime != 0.0f )
    {
        stdControl_bControlsIdle = 0;
    }
    return deltaTime;
}

int J3DAPI stdControl_ReadAxisAsKey(size_t axis, int* pbPressed)
{
    // Port: retail indexed stdControl_aAxes with the id still carrying its flag bits, far outside the array; they are
    // masked off here.
    const size_t aid = STDCONTROL_GETAID(axis);
    if ( (axis & STDCONTROL_AID_LOW_SENSITIVITY) != 0 && aid < STDCONTROL_MAX_AXES
        && (stdControl_aAxes[aid].flags & STDCONTROL_AXIS_GAMEPAD) == 0 )
    {
        return stdControl_ReadAxisAsKeyEx(axis, pbPressed, 0.75f);
    }
    return stdControl_ReadAxisAsKeyEx(axis, pbPressed, 0.25f);
}

int J3DAPI stdControl_ReadAxisAsKeyEx(size_t axis, int* pbPressed, float lowValue)
{
    (void)pbPressed; // retail never set it
    const float pos = stdControl_ReadAxis(axis);

    if ( (axis & (STDCONTROL_AID_LOW_SENSITIVITY | STDCONTROL_AID_POSITIVE_AXIS | STDCONTROL_AID_NEGATIVE_AXIS)) != 0 )
    {
        axis &= ~(size_t)STDCONTROL_AID_LOW_SENSITIVITY;
        axis &= (STDCONTROL_AID_LOW_SENSITIVITY | STDCONTROL_AID_POSITIVE_AXIS | STDCONTROL_AID_NEGATIVE_AXIS);
        if ( axis == STDCONTROL_AID_POSITIVE_AXIS && pos > lowValue )
        {
            return 1;
        }
        if ( axis == STDCONTROL_AID_NEGATIVE_AXIS && pos < -lowValue )
        {
            return 1;
        }
    }
    else if ( (pos < 0.0f ? -pos : pos) > lowValue )
    {
        return 1;
    }

    return 0;
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

    if ( stdControl_bControlsIdle && stdControl_aKeyInfo[keyNum] )
    {
        stdControl_bControlsIdle = 0;
    }
    return stdControl_aKeyInfo[keyNum];
}

void J3DAPI stdControl_FinishRead(void)
{
}

void J3DAPI stdControl_SetMouseSensitivity(float xSensitivity, float ySensitivity)
{
    stdControl_mouseXSensitivity = xSensitivity;
    stdControl_mouseYSensitivity = ySensitivity;

    if ( (stdControl_aAxes[STDCONTROL_AID_MOUSE_X].flags & STDCONTROL_AXIS_REGISTERED) != 0 )
    {
        const int xLimit = (int)(stdControl_mouseXSensitivity * 250.0f);
        stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_X, -xLimit, xLimit, 0.0f);
    }

    if ( (stdControl_aAxes[STDCONTROL_AID_MOUSE_Y].flags & STDCONTROL_AXIS_REGISTERED) != 0 )
    {
        const int yLimit = (int)(stdControl_mouseYSensitivity * 200.0f);
        stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_Y, -yLimit, yLimit, 0.0f);
    }
}

int J3DAPI stdControl_ControlsActive(void)
{
    return stdControl_bControlsActive;
}

// Retail released every key and mouse button, then acquired or released the devices. Returns 0 when the mouse was
// taken or let go (retail returned the mouse Acquire's HRESULT, DI_OK being 0) and 1 when the mouse is not read.
int J3DAPI stdControl_SetActivation(int bActive)
{
    if ( !stdControl_bOpen )
    {
        return 1;
    }

    for ( size_t i = 0; i < STDCONTROL_MAX_KEYBOARD_BUTTONS; ++i )
    {
        control_UpdateKeyState(i, 0, stdControl_curReadTime);
    }

    if ( stdControl_bReadMouse )
    {
        for ( size_t i = 0; i < STDCONTROL_MAX_MOUSE_BUTTONS; ++i )
        {
            control_UpdateKeyState(STDCONTROL_KID_MOUSE_LBUTTON + i, 0, stdControl_curReadTime);
        }
    }

    stdControl_bControlsActive = bActive ? 1 : 0;
    control_UpdateMouseCapture();
    return stdControl_bReadMouse || !bActive ? 0 : 1;
}

int J3DAPI stdControl_ToggleMouse(void)
{
    stdControl_bReadMouse = !stdControl_bReadMouse;
    control_UpdateMouseCapture();
    return stdControl_bReadMouse;
}

int J3DAPI stdControl_EnableMouse(int bEnable)
{
    if ( bEnable != stdControl_bReadMouse )
    {
        stdControl_ToggleMouse();
    }
    return 1;
}

int J3DAPI stdControl_ControlsIdle(void)
{
    return stdControl_bControlsIdle;
}

int J3DAPI stdControl_TestAxisFlag(size_t axis, StdControlAxisFlag flags)
{
    const uint32_t bPositiveAxis = (uint32_t)(axis & STDCONTROL_AID_POSITIVE_AXIS);
    const uint32_t bNegativeAxis = (uint32_t)(axis & STDCONTROL_AID_NEGATIVE_AXIS);
    axis = STDCONTROL_GETAID(axis);
    if ( axis >= STDCONTROL_MAX_AXES )
    {
        return 0;
    }

    if ( (flags & STDCONTROL_AXIS_ENABLED) != 0 )
    {
        if ( bPositiveAxis )
        {
            flags &= ~STDCONTROL_AXIS_ENABLED;
            flags |= STDCONTROL_AXIS_POSITIVE;
        }
        if ( bNegativeAxis )
        {
            flags &= ~STDCONTROL_AXIS_ENABLED;
            flags |= STDCONTROL_AXIS_NEGATIVE;
        }
    }

    return stdControl_aAxes[axis].flags & flags;
}

void J3DAPI stdControl_SetAxisFlags(size_t axis, int flags)
{
    const uint32_t bPositiveAxis = (uint32_t)(axis & STDCONTROL_AID_POSITIVE_AXIS);
    const uint32_t bNegativeAxis = (uint32_t)(axis & STDCONTROL_AID_NEGATIVE_AXIS);
    axis = STDCONTROL_GETAID(axis);
    if ( axis >= STDCONTROL_MAX_AXES )
    {
        return;
    }

    if ( (flags & STDCONTROL_AXIS_ENABLED) != 0 )
    {
        if ( bPositiveAxis )
        {
            flags |= STDCONTROL_AXIS_POSITIVE;
        }
        if ( bNegativeAxis )
        {
            flags |= STDCONTROL_AXIS_NEGATIVE;
        }
    }

    stdControl_aAxes[axis].flags |= flags;
}

static void control_UpdateKeyState(size_t keyId, int bPressed, uint32_t tickTime)
{
    if ( bPressed && !stdControl_aKeyInfo[keyId] )
    {
        stdControl_aKeyInfo[keyId]     = 1;
        stdControl_aKeyIdleTimes[keyId] = stdControl_curReadTime - tickTime;
        ++stdControl_aKeyPressed[keyId];
    }
    else if ( !bPressed && stdControl_aKeyInfo[keyId] )
    {
        stdControl_aKeyInfo[keyId] = 0;
        if ( !stdControl_aKeyIdleTimes[keyId] )
        {
            stdControl_aKeyIdleTimes[keyId] = stdControl_readDeltaTime;
        }
        stdControl_aKeyIdleTimes[keyId] -= stdControl_curReadTime - tickTime;
    }
}

static void control_EnableAxisRead(size_t axis)
{
    if ( STDCONTROL_ISMOUSEAXIS(axis) )
    {
        stdControl_bReadMouse = 1;
        control_UpdateMouseCapture();
    }
    else if ( STDCONTROL_ISJOYSTICKAXIS(axis) )
    {
        stdControl_bReadJoysticks = 1;
    }
}

static void control_ReadKeyboard(void)
{
    for ( size_t keyId = 0; keyId < STDCONTROL_MAX_KEYBOARD_BUTTONS; ++keyId )
    {
        control_UpdateKeyState(keyId, control_snapshot.aKeys[keyId] & 0x80, stdControl_curReadTime);
    }
}

static void control_ReadJoysticks(void)
{
    for ( size_t joyNum = 0; joyNum < stdControl_numJoystickDevices; joyNum++ )
    {
        const ControlPad* pPad = &control_snapshot.aPads[joyNum];

        for ( size_t axis = 0; axis < STDCONTROL_JOYSTICK_NUMAXES; axis++ )
        {
            stdControl_aAxisStates[STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axis)] = pPad->aAxes[axis];
        }

        for ( size_t i = 0; i < STDCONTROL_NUM_JOYSTICK_BUTTONS; ++i )
        {
            control_UpdateKeyState(STDCONTROL_JOYSTICK_GETBUTTON(joyNum, i), pPad->aButtons[i], stdControl_curReadTime);
        }

        // Every hat slot is updated, so a controller that left releases its directions (retail read only the hats the
        // device had, and devices never left).
        for ( size_t i = 0; i < STDCONTROL_MAX_JOYSTICK_POVCONTROLERS; ++i )
        {
            const uint32_t pov      = pPad->aPovs[i];
            const int      bCentred = LOWORD(pov) == 0xFFFF;
            control_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVLEFT(joyNum, i), !bCentred && pov >= 22500 && pov <= 31500, stdControl_curReadTime);
            control_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVUP(joyNum, i), !bCentred && (pov >= 31500 || pov <= 4500), stdControl_curReadTime);
            control_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVRIGHT(joyNum, i), !bCentred && pov >= 4500 && pov <= 13500, stdControl_curReadTime);
            control_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVDOWN(joyNum, i), !bCentred && pov >= 13500 && pov <= 22500, stdControl_curReadTime);
        }
    }
}

static void control_ReadMouse(void)
{
    if ( !stdControl_bReadMouse )
    {
        return;
    }

    const ControlSnapshot* pSnap = &control_snapshot;
    if ( stdControl_bMouseSensitivityEnabled )
    {
        size_t axis = STDCONTROL_AID_MOUSE_X;
        stdControl_aAxisStates[axis] += pSnap->mouseDx;
        stdControl_aAxisStates[axis] = STDMATH_CLAMP(stdControl_aAxisStates[axis], stdControl_aAxes[axis].min, stdControl_aAxes[axis].max);

        axis = STDCONTROL_AID_MOUSE_Y;
        stdControl_aAxisStates[axis] += pSnap->mouseDy;
        stdControl_aAxisStates[axis] = STDMATH_CLAMP(stdControl_aAxisStates[axis], stdControl_aAxes[axis].min, stdControl_aAxes[axis].max);

        axis = STDCONTROL_AID_MOUSE_Z;
        stdControl_aAxisStates[axis] += pSnap->mouseDz;
        stdControl_aAxisStates[axis] = STDMATH_CLAMP(stdControl_aAxisStates[axis], stdControl_aAxes[axis].min, stdControl_aAxes[axis].max);
    }
    else
    {
        stdControl_aAxisStates[STDCONTROL_AID_MOUSE_X] = pSnap->mouseDx;
        stdControl_aAxisStates[STDCONTROL_AID_MOUSE_Y] = pSnap->mouseDy;
        stdControl_aAxisStates[STDCONTROL_AID_MOUSE_Z] = pSnap->mouseDz;
        if ( stdControl_readDeltaTime < 25 )
        {
            // Retail smoothed fast frames by averaging with the previous read.
            stdControl_aAxisStates[STDCONTROL_AID_MOUSE_X] = (pSnap->mouseDx + stdControl_mousePos.x) >> 1;
            stdControl_aAxisStates[STDCONTROL_AID_MOUSE_Y] = (pSnap->mouseDy + stdControl_mousePos.y) >> 1;
        }

        stdControl_mousePos.x = pSnap->mouseDx;
        stdControl_mousePos.y = pSnap->mouseDy;
    }

    // The buffered button events, in order; then the state, which settles any the buffer could not hold.
    for ( uint32_t i = 0; i < pSnap->numMouseEvents && i < CONTROL_MAX_MOUSE_EVENTS; i++ )
    {
        control_UpdateKeyState(STDCONTROL_KID_MOUSE_LBUTTON + pSnap->aMouseEvents[i].button, pSnap->aMouseEvents[i].bDown,
                               stdControl_curReadTime);
    }
    for ( size_t i = 0; i < STDCONTROL_MAX_MOUSE_BUTTONS; i++ )
    {
        control_UpdateKeyState(STDCONTROL_KID_MOUSE_LBUTTON + i, pSnap->aMouseButtons[i], stdControl_curReadTime);
    }
}

void J3DAPI stdControl_RegisterAxis(size_t axisID, int min, int max, float deadzoneScale)
{
    STD_ASSERTREL(max > min);
    STD_ASSERTREL(max - min > 0);
    STD_ASSERTREL(axisID < STDCONTROL_MAX_AXES);

    const int center = min + (max - min + 1) / 2;

    stdControl_aAxes[axisID].flags |= STDCONTROL_AXIS_REGISTERED;
    stdControl_aAxes[axisID].min    = min;
    stdControl_aAxes[axisID].max    = max;
    stdControl_aAxes[axisID].center = center;
    stdControl_aAxes[axisID].scale  = 1.0f / (float)(max - center);

    if ( deadzoneScale != 0.0f )
    {
        stdControl_aAxes[axisID].deadzoneThreshold = (int)(deadzoneScale * (float)(max - center));
    }
    else
    {
        stdControl_aAxes[axisID].deadzoneThreshold = 0;
    }
}

void J3DAPI stdControl_ResetMousePos(void)
{
    stdControl_aAxisStates[STDCONTROL_AID_MOUSE_X] = 0;
    stdControl_aAxisStates[STDCONTROL_AID_MOUSE_Y] = 0;
    stdControl_aAxisStates[STDCONTROL_AID_MOUSE_Z] = 0;
}

size_t J3DAPI stdControl_GetMaxJoystickButtons(void)
{
    size_t maxButtons = 0;
    for ( size_t i = 0; i < stdControl_numJoystickDevices; i++ )
    {
        if ( control_aAppliedPads[i].numButtons > maxButtons )
        {
            maxButtons = control_aAppliedPads[i].numButtons;
        }
    }
    return maxButtons;
}

size_t J3DAPI stdControl_GetNumJoysticks(void)
{
    return stdControl_numJoystickDevices;
}

void J3DAPI stdControl_EnableMouseSensitivity(int bEnable)
{
    stdControl_bMouseSensitivityEnabled = bEnable;
}

void J3DAPI stdControl_ShowMouseCursor(int bShow)
{
    // Retail balanced Windows' display counter so the cursor ended up shown or hidden; SDL keeps a plain flag. Relative
    // mode hides it regardless while the game has the mouse.
    if ( bShow )
    {
        SDL_ShowCursor();
        stdControl_cursorDisplayCounter = 0;
    }
    else
    {
        SDL_HideCursor();
        stdControl_cursorDisplayCounter = -1;
    }
}

// Retail: a DirectInput gamepad, i.e. a digital pad, whose axes are on or off. An analog controller is not one here:
// like a joystick, its forward axis also binds run at full deflection (jonesConfig_BindJoystickControl).
int J3DAPI stdControl_IsGamePad(int joyNum)
{
    (void)joyNum;
    return 0;
}

int J3DAPI stdControl_DevicesChanged(void)
{
    const int bChanged = control_bDevicesChanged;
    control_bDevicesChanged = 0;
    return bChanged;
}

int J3DAPI stdControl_IsDualStick(int joyNum)
{
    return joyNum >= 0 && joyNum < STDCONTROL_MAX_JOYSTICK_DEVICES && control_aAppliedPads[joyNum].bPresent
        && control_aAppliedPads[joyNum].bDualStick;
}
