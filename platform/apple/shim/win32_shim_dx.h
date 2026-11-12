#ifndef J3D_PLATFORM_WIN32_SHIM_DX_H
#define J3D_PLATFORM_WIN32_SHIM_DX_H
/*
 * DirectX 6 vocabulary (DirectDraw, Direct3D, DirectInput, DirectSound, DirectPlay) still named by engine headers.
 * Part of win32_shim.h; see that file for the rules.
 *
 * COM interfaces are opaque: nothing outside the replaced platform files calls through them. The structs embedded by
 * value in std/types.h (tSystemTexture, tVSurface, StdDisplayDevice, Device3D, StdTextureFormat, StdInputDevice) keep
 * their DirectX names and the fields engine code reads, so those types still compile until the platform layer gives
 * them native replacements (Phases 2 to 4).
 *
 * Values that are part of the game's data are kept exact: DIK_* scancodes are stored in key bindings and named in the
 * .kfg keyset files (PORT-PLAN.md section 2.1), so the table below is the real DirectInput set.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------------------------------------------
 * Opaque COM interfaces
 * -------------------------------------------------------------------------------------------------------------- */
#define J3D_DX_INTERFACE(name) typedef struct name name; typedef name* LP##name
J3D_DX_INTERFACE(IUnknown);
typedef struct IDirectDraw4           IDirectDraw4,           *LPDIRECTDRAW4;
typedef struct IDirectDraw            IDirectDraw,            *LPDIRECTDRAW;
typedef struct IDirectDrawSurface4    IDirectDrawSurface4,    *LPDIRECTDRAWSURFACE4;
typedef struct IDirectDrawSurface     IDirectDrawSurface,     *LPDIRECTDRAWSURFACE;
typedef struct IDirectDrawPalette     IDirectDrawPalette,     *LPDIRECTDRAWPALETTE;
typedef struct IDirectDrawClipper     IDirectDrawClipper,     *LPDIRECTDRAWCLIPPER;
typedef struct IDirectDrawGammaControl IDirectDrawGammaControl, *LPDIRECTDRAWGAMMACONTROL;
typedef struct IDirect3D3             IDirect3D3,             *LPDIRECT3D3;
typedef struct IDirect3DDevice3       IDirect3DDevice3,       *LPDIRECT3DDEVICE3;
typedef struct IDirect3DViewport3     IDirect3DViewport3,     *LPDIRECT3DVIEWPORT3;
typedef struct IDirect3DTexture2      IDirect3DTexture2,      *LPDIRECT3DTEXTURE2;
typedef struct IDirectInputA          IDirectInputA,          *LPDIRECTINPUTA, *LPDIRECTINPUT;
typedef struct IDirectInputDeviceA    IDirectInputDeviceA,    *LPDIRECTINPUTDEVICEA, *LPDIRECTINPUTDEVICE;
typedef struct IDirectInputDevice2A   IDirectInputDevice2A,   *LPDIRECTINPUTDEVICE2A, *LPDIRECTINPUTDEVICE2;
typedef struct IDirectSound           IDirectSound,           *LPDIRECTSOUND;
typedef struct IDirectSoundBuffer     IDirectSoundBuffer,     *LPDIRECTSOUNDBUFFER;
typedef struct IDirectSound3DBuffer   IDirectSound3DBuffer,   *LPDIRECTSOUND3DBUFFER;
typedef struct IDirectSound3DListener IDirectSound3DListener, *LPDIRECTSOUND3DLISTENER;
typedef struct IDirectPlay4           IDirectPlay4,           *LPDIRECTPLAY4, *LPDIRECTPLAY4A;
typedef struct IDirectPlayLobby3      IDirectPlayLobby3,      *LPDIRECTPLAYLOBBY3, *LPDIRECTPLAYLOBBY3A;
#undef J3D_DX_INTERFACE

/* ----------------------------------------------------------------------------------------------------------------
 * Direct3D (d3dtypes.h)
 * -------------------------------------------------------------------------------------------------------------- */
typedef float    D3DVALUE, *LPD3DVALUE;
typedef uint32_t D3DCOLOR, *LPD3DCOLOR;
#define D3DVAL(val)          ((float)(val))
#define D3DDivide(a, b)      (float)((double)(a) / (double)(b))
#define D3DMultiply(a, b)    ((a) * (b))
#define RGBA_MAKE(r, g, b, a) ((D3DCOLOR)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))
#define RGB_MAKE(r, g, b)     ((D3DCOLOR)(((r) << 16) | ((g) << 8) | (b)))
#define D3DRGBA(r, g, b, a) \
    ((D3DCOLOR)((((int32_t)((a) * 255)) << 24) | (((int32_t)((r) * 255)) << 16) | (((int32_t)((g) * 255)) << 8) | (int32_t)((b) * 255)))
#define D3DRGB(r, g, b) \
    (0xff000000L | (((int32_t)((r) * 255)) << 16) | (((int32_t)((g) * 255)) << 8) | (int32_t)((b) * 255))
#define RGBA_GETALPHA(rgb) ((rgb) >> 24)
#define RGBA_GETRED(rgb)   (((rgb) >> 16) & 0xff)
#define RGBA_GETGREEN(rgb) (((rgb) >> 8) & 0xff)
#define RGBA_GETBLUE(rgb)  ((rgb) & 0xff)

/* The pre-transformed, lit vertex rdroid hands to std3D. Its layout is the contract with the Phase 2 Metal backend. */
typedef struct _D3DTLVERTEX
{
    union { D3DVALUE sx; D3DVALUE dvSX; };
    union { D3DVALUE sy; D3DVALUE dvSY; };
    union { D3DVALUE sz; D3DVALUE dvSZ; };
    union { D3DVALUE rhw; D3DVALUE dvRHW; };
    union { D3DCOLOR color; D3DCOLOR dcColor; };
    union { D3DCOLOR specular; D3DCOLOR dcSpecular; };
    union { D3DVALUE tu; D3DVALUE dvTU; };
    union { D3DVALUE tv; D3DVALUE dvTV; };
} D3DTLVERTEX, *LPD3DTLVERTEX;

typedef struct _D3DPrimCaps
{
    DWORD dwSize, dwMiscCaps, dwRasterCaps, dwZCmpCaps, dwSrcBlendCaps, dwDestBlendCaps, dwAlphaCmpCaps;
    DWORD dwShadeCaps, dwTextureCaps, dwTextureFilterCaps, dwTextureBlendCaps, dwTextureAddressCaps;
    DWORD dwStippleWidth, dwStippleHeight;
} D3DPRIMCAPS, *LPD3DPRIMCAPS;

typedef struct _D3DDeviceDesc
{
    DWORD       dwSize;
    DWORD       dwFlags;
    DWORD       dcmColorModel;
    DWORD       dwDevCaps;
    DWORD       dtcTransformCaps[2];
    BOOL        bClipping;
    DWORD       dlcLightingCaps[4];
    D3DPRIMCAPS dpcLineCaps;
    D3DPRIMCAPS dpcTriCaps;
    DWORD       dwDeviceRenderBitDepth;
    DWORD       dwDeviceZBufferBitDepth;
    DWORD       dwMaxBufferSize;
    DWORD       dwMaxVertexCount;
    DWORD       dwMinTextureWidth, dwMinTextureHeight;
    DWORD       dwMaxTextureWidth, dwMaxTextureHeight;
    DWORD       dwMinStippleWidth, dwMaxStippleWidth;
    DWORD       dwMinStippleHeight, dwMaxStippleHeight;
    DWORD       dwMaxTextureRepeat;
    DWORD       dwMaxTextureAspectRatio;
    DWORD       dwMaxAnisotropy;
    D3DVALUE    dvGuardBandLeft, dvGuardBandTop, dvGuardBandRight, dvGuardBandBottom;
    D3DVALUE    dvExtentsAdjust;
    DWORD       dwStencilCaps;
    DWORD       dwFVFCaps;
    DWORD       dwTextureOpCaps;
    WORD        wMaxTextureBlendStages;
    WORD        wMaxSimultaneousTextures;
} D3DDEVICEDESC, *LPD3DDEVICEDESC;

/* ----------------------------------------------------------------------------------------------------------------
 * DirectDraw (ddraw.h)
 * -------------------------------------------------------------------------------------------------------------- */
#define DDBD_1  0x00004000L
#define DDBD_2  0x00002000L
#define DDBD_4  0x00001000L
#define DDBD_8  0x00000800L
#define DDBD_16 0x00000400L
#define DDBD_24 0x00000200L
#define DDBD_32 0x00000100L

typedef struct _DDCOLORKEY { DWORD dwColorSpaceLowValue, dwColorSpaceHighValue; } DDCOLORKEY, *LPDDCOLORKEY;

typedef struct _DDPIXELFORMAT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    union { DWORD dwRGBBitCount; DWORD dwZBufferBitDepth; DWORD dwAlphaBitDepth; };
    union { DWORD dwRBitMask; DWORD dwStencilBitDepth; };
    union { DWORD dwGBitMask; DWORD dwZBitMask; };
    union { DWORD dwBBitMask; DWORD dwStencilBitMask; };
    union { DWORD dwRGBAlphaBitMask; DWORD dwRGBZBitMask; };
} DDPIXELFORMAT, *LPDDPIXELFORMAT;

typedef struct _DDSCAPS2 { DWORD dwCaps, dwCaps2, dwCaps3, dwCaps4; } DDSCAPS2, *LPDDSCAPS2;
typedef struct _DDSCAPS  { DWORD dwCaps; } DDSCAPS, *LPDDSCAPS;

typedef struct _DDSURFACEDESC2
{
    DWORD         dwSize;
    DWORD         dwFlags;
    DWORD         dwHeight;
    DWORD         dwWidth;
    union { LONG lPitch; DWORD dwLinearSize; };
    DWORD         dwBackBufferCount;
    union { DWORD dwMipMapCount; DWORD dwRefreshRate; };
    DWORD         dwAlphaBitDepth;
    DWORD         dwReserved;
    LPVOID        lpSurface;
    DDCOLORKEY    ddckCKDestOverlay;
    DDCOLORKEY    ddckCKDestBlt;
    DDCOLORKEY    ddckCKSrcOverlay;
    DDCOLORKEY    ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS2      ddsCaps;
    DWORD         dwTextureStage;
} DDSURFACEDESC2, *LPDDSURFACEDESC2;

typedef struct _DDCAPS_DX6
{
    DWORD    dwSize;
    DWORD    dwCaps;
    DWORD    dwCaps2;
    DWORD    dwCKeyCaps;
    DWORD    dwFXCaps;
    DWORD    dwFXAlphaCaps;
    DWORD    dwPalCaps;
    DWORD    dwSVCaps;
    DWORD    dwAlphaBltConstBitDepths;
    DWORD    dwAlphaBltPixelBitDepths;
    DWORD    dwAlphaBltSurfaceBitDepths;
    DWORD    dwAlphaOverlayConstBitDepths;
    DWORD    dwAlphaOverlayPixelBitDepths;
    DWORD    dwAlphaOverlaySurfaceBitDepths;
    DWORD    dwZBufferBitDepths;
    DWORD    dwVidMemTotal;
    DWORD    dwVidMemFree;
    DWORD    dwMaxVisibleOverlays;
    DWORD    dwCurrVisibleOverlays;
    DWORD    dwNumFourCCCodes;
    DWORD    dwAlignBoundarySrc;
    DWORD    dwAlignSizeSrc;
    DWORD    dwAlignBoundaryDest;
    DWORD    dwAlignSizeDest;
    DWORD    dwAlignStrideAlign;
    DWORD    dwRops[8];
    DDSCAPS  ddsOldCaps;
    DWORD    dwMinOverlayStretch;
    DWORD    dwMaxOverlayStretch;
    DWORD    dwMinLiveVideoStretch;
    DWORD    dwMaxLiveVideoStretch;
    DWORD    dwMinHwCodecStretch;
    DWORD    dwMaxHwCodecStretch;
    DWORD    dwReserved1, dwReserved2, dwReserved3;
    DWORD    dwSVBCaps, dwSVBCKeyCaps, dwSVBFXCaps;
    DWORD    dwSVBRops[8];
    DWORD    dwVSBCaps, dwVSBCKeyCaps, dwVSBFXCaps;
    DWORD    dwVSBRops[8];
    DWORD    dwSSBCaps, dwSSBCKeyCaps, dwSSBFXCaps;
    DWORD    dwSSBRops[8];
    DWORD    dwMaxVideoPorts;
    DWORD    dwCurrVideoPorts;
    DWORD    dwSVBCaps2;
    DWORD    dwNLVBCaps, dwNLVBCaps2, dwNLVBCKeyCaps, dwNLVBFXCaps;
    DWORD    dwNLVBRops[8];
    DDSCAPS2 ddsCaps;
} DDCAPS_DX6, DDCAPS, *LPDDCAPS_DX6, *LPDDCAPS;

/* ----------------------------------------------------------------------------------------------------------------
 * DirectInput (dinput.h)
 * -------------------------------------------------------------------------------------------------------------- */
typedef struct DIDEVCAPS
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDevType;
    DWORD dwAxes;
    DWORD dwButtons;
    DWORD dwPOVs;
    DWORD dwFFSamplePeriod;
    DWORD dwFFMinTimeResolution;
    DWORD dwFirmwareRevision;
    DWORD dwHardwareRevision;
    DWORD dwFFDriverVersion;
} DIDEVCAPS, *LPDIDEVCAPS;

typedef struct DIDEVICEINSTANCEA
{
    DWORD dwSize;
    GUID  guidInstance;
    GUID  guidProduct;
    DWORD dwDevType;
    CHAR  tszInstanceName[MAX_PATH];
    CHAR  tszProductName[MAX_PATH];
    GUID  guidFFDriver;
    WORD  wUsagePage;
    WORD  wUsage;
} DIDEVICEINSTANCEA, DIDEVICEINSTANCE, *LPDIDEVICEINSTANCEA, *LPDIDEVICEINSTANCE;
typedef const DIDEVICEINSTANCEA* LPCDIDEVICEINSTANCEA;

typedef struct _DIJOYSTATE
{
    LONG  lX, lY, lZ, lRx, lRy, lRz;
    LONG  rglSlider[2];
    DWORD rgdwPOV[4];
    BYTE  rgbButtons[32];
} DIJOYSTATE, *LPDIJOYSTATE;

typedef struct _DIMOUSESTATE
{
    LONG lX, lY, lZ;
    BYTE rgbButtons[4];
} DIMOUSESTATE, *LPDIMOUSESTATE;

typedef struct DIDEVICEOBJECTDATA
{
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;

#define DIJOFS_X            offsetof(DIJOYSTATE, lX)
#define DIJOFS_Y            offsetof(DIJOYSTATE, lY)
#define DIJOFS_Z            offsetof(DIJOYSTATE, lZ)
#define DIJOFS_RX           offsetof(DIJOYSTATE, lRx)
#define DIJOFS_RY           offsetof(DIJOYSTATE, lRy)
#define DIJOFS_RZ           offsetof(DIJOYSTATE, lRz)
#define DIJOFS_SLIDER(n)    (offsetof(DIJOYSTATE, rglSlider) + (n) * sizeof(LONG))
#define DIJOFS_POV(n)       (offsetof(DIJOYSTATE, rgdwPOV) + (n) * sizeof(DWORD))
#define DIJOFS_BUTTON(n)    (offsetof(DIJOYSTATE, rgbButtons) + (n))

/* DirectInput keyboard scancodes (PC set 1). Stored in key bindings and .kfg files: values must not change. */
#define DIK_ESCAPE          0x01
#define DIK_1               0x02
#define DIK_2               0x03
#define DIK_3               0x04
#define DIK_4               0x05
#define DIK_5               0x06
#define DIK_6               0x07
#define DIK_7               0x08
#define DIK_8               0x09
#define DIK_9               0x0A
#define DIK_0               0x0B
#define DIK_MINUS           0x0C
#define DIK_EQUALS          0x0D
#define DIK_BACK            0x0E
#define DIK_TAB             0x0F
#define DIK_Q               0x10
#define DIK_W               0x11
#define DIK_E               0x12
#define DIK_R               0x13
#define DIK_T               0x14
#define DIK_Y               0x15
#define DIK_U               0x16
#define DIK_I               0x17
#define DIK_O               0x18
#define DIK_P               0x19
#define DIK_LBRACKET        0x1A
#define DIK_RBRACKET        0x1B
#define DIK_RETURN          0x1C
#define DIK_LCONTROL        0x1D
#define DIK_A               0x1E
#define DIK_S               0x1F
#define DIK_D               0x20
#define DIK_F               0x21
#define DIK_G               0x22
#define DIK_H               0x23
#define DIK_J               0x24
#define DIK_K               0x25
#define DIK_L               0x26
#define DIK_SEMICOLON       0x27
#define DIK_APOSTROPHE      0x28
#define DIK_GRAVE           0x29
#define DIK_LSHIFT          0x2A
#define DIK_BACKSLASH       0x2B
#define DIK_Z               0x2C
#define DIK_X               0x2D
#define DIK_C               0x2E
#define DIK_V               0x2F
#define DIK_B               0x30
#define DIK_N               0x31
#define DIK_M               0x32
#define DIK_COMMA           0x33
#define DIK_PERIOD          0x34
#define DIK_SLASH           0x35
#define DIK_RSHIFT          0x36
#define DIK_MULTIPLY        0x37
#define DIK_LMENU           0x38
#define DIK_SPACE           0x39
#define DIK_CAPITAL         0x3A
#define DIK_F1              0x3B
#define DIK_F2              0x3C
#define DIK_F3              0x3D
#define DIK_F4              0x3E
#define DIK_F5              0x3F
#define DIK_F6              0x40
#define DIK_F7              0x41
#define DIK_F8              0x42
#define DIK_F9              0x43
#define DIK_F10             0x44
#define DIK_NUMLOCK         0x45
#define DIK_SCROLL          0x46
#define DIK_NUMPAD7         0x47
#define DIK_NUMPAD8         0x48
#define DIK_NUMPAD9         0x49
#define DIK_SUBTRACT        0x4A
#define DIK_NUMPAD4         0x4B
#define DIK_NUMPAD5         0x4C
#define DIK_NUMPAD6         0x4D
#define DIK_ADD             0x4E
#define DIK_NUMPAD1         0x4F
#define DIK_NUMPAD2         0x50
#define DIK_NUMPAD3         0x51
#define DIK_NUMPAD0         0x52
#define DIK_DECIMAL         0x53
#define DIK_OEM_102         0x56
#define DIK_F11             0x57
#define DIK_F12             0x58
#define DIK_F13             0x64
#define DIK_F14             0x65
#define DIK_F15             0x66
#define DIK_KANA            0x70
#define DIK_ABNT_C1         0x73
#define DIK_CONVERT         0x79
#define DIK_NOCONVERT       0x7B
#define DIK_YEN             0x7D
#define DIK_ABNT_C2         0x7E
#define DIK_NUMPADEQUALS    0x8D
#define DIK_PREVTRACK       0x90
#define DIK_CIRCUMFLEX      DIK_PREVTRACK
#define DIK_AT              0x91
#define DIK_COLON           0x92
#define DIK_UNDERLINE       0x93
#define DIK_KANJI           0x94
#define DIK_STOP            0x95
#define DIK_AX              0x96
#define DIK_UNLABELED       0x97
#define DIK_NEXTTRACK       0x99
#define DIK_NUMPADENTER     0x9C
#define DIK_RCONTROL        0x9D
#define DIK_MUTE            0xA0
#define DIK_CALCULATOR      0xA1
#define DIK_PLAYPAUSE       0xA2
#define DIK_MEDIASTOP       0xA4
#define DIK_VOLUMEDOWN      0xAE
#define DIK_VOLUMEUP        0xB0
#define DIK_WEBHOME         0xB2
#define DIK_NUMPADCOMMA     0xB3
#define DIK_DIVIDE          0xB5
#define DIK_SYSRQ           0xB7
#define DIK_RMENU           0xB8
#define DIK_PAUSE           0xC5
#define DIK_HOME            0xC7
#define DIK_UP              0xC8
#define DIK_PRIOR           0xC9
#define DIK_LEFT            0xCB
#define DIK_RIGHT           0xCD
#define DIK_END             0xCF
#define DIK_DOWN            0xD0
#define DIK_NEXT            0xD1
#define DIK_INSERT          0xD2
#define DIK_DELETE          0xD3
#define DIK_LWIN            0xDB
#define DIK_RWIN            0xDC
#define DIK_APPS            0xDD
#define DIK_POWER           0xDE
#define DIK_SLEEP           0xDF
#define DIK_BACKSPACE       DIK_BACK
#define DIK_NUMPADSTAR      DIK_MULTIPLY
#define DIK_LALT            DIK_LMENU
#define DIK_CAPSLOCK        DIK_CAPITAL
#define DIK_NUMPADMINUS     DIK_SUBTRACT
#define DIK_NUMPADPLUS      DIK_ADD
#define DIK_NUMPADPERIOD    DIK_DECIMAL
#define DIK_NUMPADSLASH     DIK_DIVIDE
#define DIK_RALT            DIK_RMENU
#define DIK_UPARROW         DIK_UP
#define DIK_PGUP            DIK_PRIOR
#define DIK_LEFTARROW       DIK_LEFT
#define DIK_RIGHTARROW      DIK_RIGHT
#define DIK_DOWNARROW       DIK_DOWN
#define DIK_PGDN            DIK_NEXT

/* ----------------------------------------------------------------------------------------------------------------
 * DirectSound (dsound.h, mmreg.h)
 * -------------------------------------------------------------------------------------------------------------- */
#define WAVE_FORMAT_PCM 1
typedef struct tWAVEFORMATEX
{
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX* LPCWAVEFORMATEX;

typedef struct _DSBUFFERDESC
{
    DWORD          dwSize;
    DWORD          dwFlags;
    DWORD          dwBufferBytes;
    DWORD          dwReserved;
    LPWAVEFORMATEX lpwfxFormat;
} DSBUFFERDESC, *LPDSBUFFERDESC;

#define DS_OK            S_OK
#define DSBVOLUME_MIN    (-10000)
#define DSBVOLUME_MAX    0
#define DSBPAN_LEFT      (-10000)
#define DSBPAN_CENTER    0
#define DSBPAN_RIGHT     10000
#define DSBFREQUENCY_MIN 100
#define DSBFREQUENCY_MAX 100000

/* ----------------------------------------------------------------------------------------------------------------
 * DirectPlay (dplay.h). The game has no multiplayer, but DPID is the player id type throughout sith's message layer.
 * -------------------------------------------------------------------------------------------------------------- */
typedef DWORD DPID, *LPDPID;
#define DPID_SYSMSG        0
#define DPID_ALLPLAYERS    0
#define DPID_SERVERPLAYER  1
#define DPID_UNKNOWN       0xFFFFFFFF
#define DPSEND_GUARANTEED  0x00000001
#define DPSEND_HIGHPRIORITY 0x00000002
#define DPSEND_OPENSTREAM  0x00000008
#define DPSEND_CLOSESTREAM 0x00000010
#define DPSEND_ASYNC       0x00000200
#define DPRECEIVE_ALL      0x00000001
#define DPRECEIVE_TOPLAYER 0x00000002
#define DPRECEIVE_FROMPLAYER 0x00000004
#define DPRECEIVE_PEEK     0x00000008
#define DP_OK              S_OK

#ifdef __cplusplus
}
#endif

#endif /* J3D_PLATFORM_WIN32_SHIM_DX_H */
