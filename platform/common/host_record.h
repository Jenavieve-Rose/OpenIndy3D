#ifndef J3D_PLATFORM_HOST_RECORD_H
#define J3D_PLATFORM_HOST_RECORD_H
/*
 * Input recording and replay by frame (PORT-PLAN.md P3.10), and scripted input for tests.
 *
 *   jones3d --record <file>   plays normally and writes what the game received from outside to <file>
 *   jones3d --replay <file>   plays <file> back instead of reading the keyboard, mouse and controllers, then closes the
 *                             game; with --headless it runs as fast as the host allows
 *   jones3d --input-script <file>   feeds the input a text script describes (below), for tests
 *
 * A run is repeatable when every value it reads from outside is: the engine's random generator is seeded the same way
 * every run (P1.24), so what remains is the clock and the input. The recording holds, in the order the game asked for
 * them:
 *
 *   CLOCK   the clock at each present. A recording runs on the frame clock (host_clock.h), which only moves at a
 *           present and in Sleep, so these values are the only ones the game saw; a replay sets them again.
 *   PUMP    each pass of the window event pump (wkernel_PeekProcessEvents, wkernel_ProcessEvents)
 *   MSG     each window message that came from outside the game and was dispatched in that pass: keys, characters,
 *           mouse, activation, timers. Messages the game posts to itself are not recorded; it posts them again.
 *   INPUT   stdControl's device snapshot at each stdControl_ReadControls (keyboard, mouse, controllers)
 *
 * A replay checks that the game asks for the same kind of record at each step. If it does not, the runs have diverged
 * (different game data or settings, or a change to the engine): the replay stops, reports the frame, and the program
 * exits with failure. The format is the host's byte order and is not meant to outlive a build.
 *
 * The input script is a text file of lines "<frame> <event> <args>", frame being the number of presents so far:
 *
 *   key <name> down|up          a keyboard key, by DIK name or key name (host_keys.h): "UP", "LCONTROL", "Space"
 *   mouse <dx> <dy> [<dz>]      relative mouse motion read at that frame
 *   button <1-4> down|up        a mouse button
 *   pad <1-32> down|up          a button of a scripted controller in slot 0 (the first pad event adds it)
 *   axis <x|y|z|rx|ry|rz> <v>   an axis of that controller, -32768 to 32767
 *   hat <center|up|right|down|left>   its hat switch
 *   vkey <F1-F12|ESCAPE|RETURN|...>   a WM_KEYDOWN and WM_KEYUP for the game window (quicksave is F5)
 *
 * '#' starts a comment. The script may be combined with --record, which is how the tests make a recording.
 */
#include <stddef.h>
#include <stdint.h>

typedef enum J3DScriptEventType
{
    J3DSCRIPT_KEY,
    J3DSCRIPT_MOUSE,
    J3DSCRIPT_BUTTON,
    J3DSCRIPT_PAD,
    J3DSCRIPT_AXIS,
    J3DSCRIPT_HAT,
    J3DSCRIPT_VKEY,
} J3DScriptEventType;

typedef struct J3DScriptEvent
{
    uint32_t           frame;
    J3DScriptEventType type;
    int32_t            a; // key: DIK; mouse: dx; button/pad: number from 1; axis: index; hat: direction (0 center, 1 up,
                          // 2 right, 3 down, 4 left); vkey: VK code
    int32_t            b; // key/button/pad: 1 down, 0 up; mouse: dy; axis: value
    int32_t            c; // mouse: dz
} J3DScriptEvent;

// Reads J3D_RECORD, J3D_REPLAY and J3D_INPUT_SCRIPT and opens the files; picks the frame clock for a recording and the
// virtual clock for a replay. Returns 0 on success (including when none is set). Called once by the app entry point,
// before the game starts.
int  J3DRecord_Startup(void);
void J3DRecord_Shutdown(void);

int J3DRecord_IsRecording(void);
int J3DRecord_IsReplaying(void);
int J3DRecord_HasScript(void);

// Nonzero once the run should end: the replay has played everything, or went out of step (J3DRecord_Failed).
int J3DRecord_ShouldQuit(void);
int J3DRecord_Failed(void);

// Presents so far.
uint32_t J3DRecord_GetFrame(void);

// Called by the display backend at every present, in place of moving the clock itself. Recording: brings the frame
// clock up to date and stores it. Replay: sets the stored value. Otherwise, on the headless display (bHeadless), moves
// the virtual clock by the fixed headless frame time.
void J3DRecord_Present(int bHeadless);

// stdControl's device snapshot, at each read. Recording: stores *pState. Replay: replaces *pState with the stored one
// and returns 1; returns 0 when not replaying.
int J3DRecord_Input(void* pState, size_t size);

// The start of a pass of the window event pump.
void J3DRecord_Pump(void);

// Recording: stores a window message that came from outside the game.
void J3DRecord_Message(uint32_t msg, uint64_t wParam, int64_t lParam);

// Replay: the next message of this pump pass. Returns 0 when the pass has no more.
int J3DRecord_NextMessage(uint32_t* pMsg, uint64_t* pWParam, int64_t* pLParam);

// The next script event due by now (its frame is no later than J3DRecord_GetFrame), in file order, for one of two
// consumers: bMessages 0 takes the device events (stdControl), 1 the vkey events (the event pump). Returns 0 when
// none is due. Each event is taken once.
int J3DRecord_TakeScriptEvent(int bMessages, J3DScriptEvent* pEvent);

// Whether the script uses a controller (pad, axis or hat events), so stdControl adds a scripted one in slot 0.
int J3DRecord_ScriptHasPad(void);

#endif
