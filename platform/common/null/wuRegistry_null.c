/*
 * wuRegistry as an in-memory key/value store (PORT-PLAN.md Phase 0). Same contract as the registry version: Save*
 * return 0 on success, Get* return the stored value or the caller's default, GetStr returns 0 when the key exists and
 * 1 when it copied the default. Values last for the process; Phase 1 persists them to
 * ~/Library/Application Support/<AppName>/settings.ini under the same key names.
 *
 * Phase 0 seeds three values so a headless run gets past the parts that need a GUI:
 *   "Start Mode" = 0 (start game). The default, 2, opens the developer dialog, which needs Phase 6.
 *   "Debug Mode" = 1 (console). The engine log goes to stdout through stdConsole.
 *   "Verbosity"  = 2 (verbose).
 * Each can be overridden with an environment variable named J3D_REG_ followed by the key with spaces replaced by
 * underscores, for example J3D_REG_Start_Mode=2.
 */
#include <stdlib.h>
#include <string.h>

#include <w32util/wuRegistry.h>

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
        pValue->pNext = wuRegistry_pValues;
        wuRegistry_pValues = pValue;
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

int J3DAPI wuRegistry_Startup(HKEY hKey, LPCSTR lpSubKey)
{
    (void)hKey; (void)lpSubKey;
    if ( !wuRegistry_bStarted )
    {
        wuRegistry_bStarted = 1;
        wuRegistry_SaveInt("Start Mode", 0);
        wuRegistry_SaveInt("Debug Mode", 1);
        wuRegistry_SaveInt("Verbosity", 2);
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
    return 0;
}

int J3DAPI wuRegistry_SaveFloat(const char* pKey, float value)
{
    RegValue* pValue = wuRegistry_Put(pKey, REGVAL_FLOAT);
    if ( !pValue )
    {
        return 1;
    }
    pValue->floatValue = value;
    return 0;
}

int J3DAPI wuRegistry_GetInt(const char* pKey, int defaultValue)
{
    const char* pEnv = wuRegistry_GetEnv(pKey);
    if ( pEnv )
    {
        return atoi(pEnv);
    }
    const RegValue* pValue = wuRegistry_Find(pKey);
    return pValue && pValue->type == REGVAL_INT ? pValue->intValue : defaultValue;
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

int J3DAPI wuRegistry_SaveIntEx(const char* pKey, int defaultValue)
{
    return wuRegistry_SaveInt(pKey, defaultValue);
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
    return pValue->pStrValue ? 0 : 1;
}

int J3DAPI wuRegistry_GetStr(const char* pKey, char* pDstStr, size_t size, const char* pDefaultValue)
{
    const char* pFound = wuRegistry_GetEnv(pKey);
    if ( !pFound )
    {
        const RegValue* pValue = wuRegistry_Find(pKey);
        pFound = pValue && pValue->type == REGVAL_STR ? pValue->pStrValue : NULL;
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
