/*
 * wuRegistry over a settings file (PORT-PLAN.md Phase 1, "Settings").
 *
 * The retail game kept its settings under HKLM\Software\LucasArts Entertainment Company LLC\Indiana Jones and the
 * Infernal Machine\v1.0. The port keeps the same value names in a text file, by default
 *     ~/Library/Application Support/J3DPort/settings.ini
 * (J3D_SETTINGS overrides the path). One value per line, "<name>=<type>:<value>" with type i (int), f (float) or s
 * (string); lines starting with ';' or '#' are comments. Every Save* rewrites the file, as RegSetValueEx wrote through.
 *
 * Same contract as the registry version: Save* return 0 on success, Get* return the stored value or the caller's
 * default, GetStr returns 0 when the key exists and 1 when it copied the default.
 *
 * Lookup order for Get*: the J3D_REG_<name> environment variable (spaces in the name become underscores, for example
 * J3D_REG_Start_Mode=2), then the file, then derived defaults (below), then the caller's default.
 *
 * Game data root. "Data Root" names the folder holding RESOURCE/ (the player's install, or the DREAMM copy). It comes
 * from J3D_DATA (main.c sets it from --data), or from the file. When set, it supplies the defaults for the two path
 * values JonesMain and JonesFile read from: "Source Dir" (the CD root; JonesFile appends "resource") and "User Path"
 * (the install root). The defaults end in '/', as the engine concatenates onto them.
 *
 * "Install Path" is the one the engine writes under (SaveGames/, ScreenShots/, KeySets/). Retail kept it in the game
 * folder; the port defaults it to ~/Library/Application Support/J3DPort/ (host_dirs.h), so nothing is written into the
 * player's game data.
 *
 * Until the dialogs existed (Phase 6), "Start Mode", "Debug Mode" and "Verbosity" defaulted to 0, 1 and 2 (start the
 * game, log to the console, verbose) so a run got past the launcher. Retail's own defaults are back (P6.23): the
 * launcher, no log, normal verbosity. The tests and tools set the values they need (J3D_REG_Start_Mode=0 and so on).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <w32util/wuRegistry.h>

#include <platform/common/host_dirs.h>

#define WUREG_APPNAME   "J3DPort"
#define WUREG_MAXLINE   1024

typedef enum
{
    REGVAL_INT,
    REGVAL_FLOAT,
    REGVAL_STR,
} RegValueType;

typedef struct RegValue
{
    struct RegValue* pNext;
    char*            pKey;
    RegValueType     type;
    int              intValue;
    float            floatValue;
    char*            pStrValue;
} RegValue;

static RegValue* wuRegistry_pValues  = NULL;
static int       wuRegistry_bStarted = 0;
static char      wuRegistry_aPath[1024];
static char      wuRegistry_aDataRoot[1024];

static RegValue* wuRegistry_Find(const char* pKey)
{
    for ( RegValue* pValue = wuRegistry_pValues; pValue; pValue = pValue->pNext )
    {
        if ( strcasecmp(pValue->pKey, pKey) == 0 ) // registry value names are case-insensitive
        {
            return pValue;
        }
    }
    return NULL;
}

static RegValue* wuRegistry_Put(const char* pKey, RegValueType type)
{
    RegValue* pValue = wuRegistry_Find(pKey);
    if ( !pValue )
    {
        pValue = calloc(1, sizeof(RegValue));
        if ( !pValue )
        {
            return NULL;
        }
        pValue->pKey = strdup(pKey);
        if ( !pValue->pKey )
        {
            free(pValue);
            return NULL;
        }

        // Append, so the file keeps the order values were first written in.
        RegValue** ppTail = &wuRegistry_pValues;
        while ( *ppTail )
        {
            ppTail = &(*ppTail)->pNext;
        }
        *ppTail = pValue;
    }
    free(pValue->pStrValue);
    pValue->pStrValue = NULL;
    pValue->type = type;
    return pValue;
}

// J3D_REG_<key> environment override; the value is parsed by the type the caller asks for.
static const char* wuRegistry_GetEnv(const char* pKey)
{
    char aName[160] = "J3D_REG_";
    size_t len = strlen(aName);
    for ( size_t i = 0; pKey[i] && len < sizeof(aName) - 1; ++i )
    {
        aName[len++] = pKey[i] == ' ' ? '_' : pKey[i];
    }
    aName[len] = '\0';
    return getenv(aName);
}

/* ----------------------------------------------------------------------------------------------------------------
 * Defaults that are not stored.
 * -------------------------------------------------------------------------------------------------------------- */
static int wuRegistry_GetDefaultInt(const char* pKey, int* pValue)
{
    // none since P6.23; kept for the next value that needs one
    static const struct { const char* pKey; int value; } aDefaults[] = {
        { "", 0 },
    };

    for ( size_t i = 0; i < sizeof(aDefaults) / sizeof(aDefaults[0]); ++i )
    {
        if ( strcasecmp(aDefaults[i].pKey, pKey) == 0 )
        {
            *pValue = aDefaults[i].value;
            return 1;
        }
    }
    return 0;
}

static const char* wuRegistry_GetDefaultStr(const char* pKey)
{
    if ( strcasecmp(pKey, "Install Path") == 0 )
    {
        return J3DHost_GetUserDir(); // where saves, screenshots and keysets are written (PORT-PLAN.md P1.19)
    }

    if ( !wuRegistry_aDataRoot[0] )
    {
        return NULL;
    }

    if ( strcasecmp(pKey, "Source Dir") == 0 || strcasecmp(pKey, "User Path") == 0 )
    {
        return wuRegistry_aDataRoot;
    }
    return NULL;
}

/* ----------------------------------------------------------------------------------------------------------------
 * The file.
 * -------------------------------------------------------------------------------------------------------------- */
static int wuRegistry_MakeDirs(const char* pPath)
{
    char aDir[1024];
    strncpy(aDir, pPath, sizeof(aDir) - 1);
    aDir[sizeof(aDir) - 1] = '\0';

    for ( char* p = aDir + 1; *p; ++p )
    {
        if ( *p == '/' )
        {
            *p = '\0';
            if ( mkdir(aDir, 0755) != 0 && errno != EEXIST )
            {
                return 1;
            }
            *p = '/';
        }
    }
    return 0;
}

static void wuRegistry_ResolvePath(void)
{
    const char* pOverride = getenv("J3D_SETTINGS");
    if ( pOverride && *pOverride )
    {
        snprintf(wuRegistry_aPath, sizeof(wuRegistry_aPath), "%s", pOverride);
        return;
    }

    const char* pHome = getenv("HOME");
    snprintf(wuRegistry_aPath, sizeof(wuRegistry_aPath), "%s/Library/Application Support/" WUREG_APPNAME "/settings.ini", pHome ? pHome : ".");
}

static char* wuRegistry_Trim(char* pStr)
{
    while ( *pStr == ' ' || *pStr == '\t' )
    {
        ++pStr;
    }
    size_t len = strlen(pStr);
    while ( len && (pStr[len - 1] == ' ' || pStr[len - 1] == '\t' || pStr[len - 1] == '\r' || pStr[len - 1] == '\n') )
    {
        pStr[--len] = '\0';
    }
    return pStr;
}

static void wuRegistry_Load(void)
{
    FILE* pFile = fopen(wuRegistry_aPath, "r");
    if ( !pFile )
    {
        return; // first run
    }

    char aLine[WUREG_MAXLINE];
    while ( fgets(aLine, sizeof(aLine), pFile) )
    {
        char* pLine = wuRegistry_Trim(aLine);
        if ( !*pLine || *pLine == ';' || *pLine == '#' )
        {
            continue;
        }

        char* pEq = strchr(pLine, '=');
        if ( !pEq || pEq[1] == '\0' || pEq[2] != ':' )
        {
            fprintf(stderr, "%s: ignoring malformed line: %s\n", wuRegistry_aPath, pLine);
            continue;
        }

        *pEq = '\0';
        const char* pKey  = wuRegistry_Trim(pLine);
        const char  type  = pEq[1];
        const char* pText = pEq + 3; // strings keep their spaces

        RegValue* pValue;
        switch ( type )
        {
            case 'i':
                if ( (pValue = wuRegistry_Put(pKey, REGVAL_INT)) )
                {
                    pValue->intValue = (int)strtol(pText, NULL, 10);
                }
                break;
            case 'f':
                if ( (pValue = wuRegistry_Put(pKey, REGVAL_FLOAT)) )
                {
                    pValue->floatValue = strtof(pText, NULL);
                }
                break;
            case 's':
                if ( (pValue = wuRegistry_Put(pKey, REGVAL_STR)) )
                {
                    pValue->pStrValue = strdup(pText);
                }
                break;
            default:
                fprintf(stderr, "%s: ignoring value of unknown type '%c': %s\n", wuRegistry_aPath, type, pKey);
                break;
        }
    }

    fclose(pFile);
}

// Writes every value to a temporary file and renames it over the settings file, so a crash never leaves it half written.
static int wuRegistry_Store(void)
{
    if ( !wuRegistry_bStarted || !wuRegistry_aPath[0] )
    {
        return 0; // Save* before Startup: kept in memory only
    }

    if ( wuRegistry_MakeDirs(wuRegistry_aPath) )
    {
        return 1;
    }

    char aTmpPath[sizeof(wuRegistry_aPath) + 8];
    snprintf(aTmpPath, sizeof(aTmpPath), "%s.tmp", wuRegistry_aPath);

    FILE* pFile = fopen(aTmpPath, "w");
    if ( !pFile )
    {
        return 1;
    }

    fprintf(pFile, "; " WUREG_APPNAME " settings. One value per line: <name>=<type>:<value>, type i (int), f (float) or s (string).\n");
    for ( const RegValue* pValue = wuRegistry_pValues; pValue; pValue = pValue->pNext )
    {
        switch ( pValue->type )
        {
            case REGVAL_INT:
                fprintf(pFile, "%s=i:%d\n", pValue->pKey, pValue->intValue);
                break;
            case REGVAL_FLOAT:
                fprintf(pFile, "%s=f:%.9g\n", pValue->pKey, (double)pValue->floatValue);
                break;
            case REGVAL_STR:
                fprintf(pFile, "%s=s:%s\n", pValue->pKey, pValue->pStrValue ? pValue->pStrValue : "");
                break;
        }
    }

    if ( fclose(pFile) != 0 || rename(aTmpPath, wuRegistry_aPath) != 0 )
    {
        unlink(aTmpPath);
        return 1;
    }
    return 0;
}

const char* J3DHost_GetDataRoot(void)
{
    return wuRegistry_aDataRoot;
}

// "Data Root" from J3D_DATA or the file, normalised to end in exactly one '/'.
static void wuRegistry_ResolveDataRoot(void)
{
    const char* pRoot = getenv("J3D_DATA");
    if ( !pRoot || !*pRoot )
    {
        const RegValue* pValue = wuRegistry_Find("Data Root");
        pRoot = pValue && pValue->type == REGVAL_STR ? pValue->pStrValue : NULL;
    }

    wuRegistry_aDataRoot[0] = '\0';
    if ( !pRoot || !*pRoot )
    {
        return;
    }

    size_t len = strlen(pRoot);
    while ( len > 1 && pRoot[len - 1] == '/' )
    {
        --len;
    }
    if ( len + 2 > sizeof(wuRegistry_aDataRoot) )
    {
        fprintf(stderr, "wuRegistry: data root path is too long: %s\n", pRoot);
        return;
    }
    memcpy(wuRegistry_aDataRoot, pRoot, len);
    wuRegistry_aDataRoot[len]     = '/';
    wuRegistry_aDataRoot[len + 1] = '\0';
}

/* ----------------------------------------------------------------------------------------------------------------
 * Public API.
 * -------------------------------------------------------------------------------------------------------------- */
int J3DAPI wuRegistry_Startup(HKEY hKey, LPCSTR lpSubKey)
{
    (void)hKey; (void)lpSubKey; // one settings file per user, whatever hive and key the engine names
    if ( !wuRegistry_bStarted )
    {
        wuRegistry_ResolvePath();
        wuRegistry_Load();
        wuRegistry_ResolveDataRoot();
        wuRegistry_bStarted = 1;
    }
    return 0;
}

void J3DAPI wuRegistry_Shutdown(void)
{
    while ( wuRegistry_pValues )
    {
        RegValue* pNext = wuRegistry_pValues->pNext;
        free(wuRegistry_pValues->pKey);
        free(wuRegistry_pValues->pStrValue);
        free(wuRegistry_pValues);
        wuRegistry_pValues = pNext;
    }
    wuRegistry_aDataRoot[0] = '\0';
    wuRegistry_bStarted = 0;
}

int J3DAPI wuRegistry_SaveInt(const char* pKey, int value)
{
    RegValue* pValue = wuRegistry_Put(pKey, REGVAL_INT);
    if ( !pValue )
    {
        return 1;
    }
    pValue->intValue = value;
    return wuRegistry_Store();
}

int J3DAPI wuRegistry_SaveFloat(const char* pKey, float value)
{
    RegValue* pValue = wuRegistry_Put(pKey, REGVAL_FLOAT);
    if ( !pValue )
    {
        return 1;
    }
    pValue->floatValue = value;
    return wuRegistry_Store();
}

int J3DAPI wuRegistry_GetInt(const char* pKey, int defaultValue)
{
    const char* pEnv = wuRegistry_GetEnv(pKey);
    if ( pEnv )
    {
        return atoi(pEnv);
    }

    const RegValue* pValue = wuRegistry_Find(pKey);
    if ( pValue && pValue->type == REGVAL_INT )
    {
        return pValue->intValue;
    }

    int value;
    return wuRegistry_GetDefaultInt(pKey, &value) ? value : defaultValue;
}

float J3DAPI wuRegistry_GetFloat(const char* pKey, float defaultValue)
{
    const char* pEnv = wuRegistry_GetEnv(pKey);
    if ( pEnv )
    {
        return (float)atof(pEnv);
    }
    const RegValue* pValue = wuRegistry_Find(pKey);
    return pValue && pValue->type == REGVAL_FLOAT ? pValue->floatValue : defaultValue;
}

// The Ex variants stored REG_BINARY in retail; the value is the same int.
int J3DAPI wuRegistry_SaveIntEx(const char* pKey, int value)
{
    return wuRegistry_SaveInt(pKey, value);
}

int J3DAPI wuRegistry_GetIntEx(const char* pKey, int defaultValue)
{
    return wuRegistry_GetInt(pKey, defaultValue);
}

int J3DAPI wuRegistry_SaveStr(const char* pKey, const char* pStr)
{
    RegValue* pValue = wuRegistry_Put(pKey, REGVAL_STR);
    if ( !pValue )
    {
        return 1;
    }
    pValue->pStrValue = strdup(pStr ? pStr : "");
    if ( !pValue->pStrValue )
    {
        return 1;
    }

    if ( strcasecmp(pKey, "Data Root") == 0 && !getenv("J3D_DATA") )
    {
        wuRegistry_ResolveDataRoot();
    }
    return wuRegistry_Store();
}

int J3DAPI wuRegistry_GetStr(const char* pKey, char* pDstStr, size_t size, const char* pDefaultValue)
{
    const char* pFound = wuRegistry_GetEnv(pKey);
    if ( !pFound )
    {
        const RegValue* pValue = wuRegistry_Find(pKey);
        pFound = pValue && pValue->type == REGVAL_STR ? pValue->pStrValue : NULL;
    }
    if ( !pFound )
    {
        pFound = wuRegistry_GetDefaultStr(pKey);
    }

    if ( pFound )
    {
        strncpy(pDstStr, pFound, size - 1);
        pDstStr[size - 1] = '\0';
        return 0;
    }

    if ( pDefaultValue )
    {
        strncpy(pDstStr, pDefaultValue, size - 1);
        pDstStr[size - 1] = '\0';
    }
    return 1;
}
