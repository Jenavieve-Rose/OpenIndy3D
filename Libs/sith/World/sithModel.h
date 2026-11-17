#ifndef SITH_SITHMODEL_H
#define SITH_SITHMODEL_H
#include <j3dcore/j3d.h>

#include <rdroid/types.h>

#include <sith/types.h>
#include <sith/Main/sithMain.h>

J3D_EXTERN_C_START

int J3DAPI sithModel_Startup(void);
void J3DAPI sithModel_Shutdown(void);

#ifdef J3D_DEBUG
int J3DAPI sithModel_WriteStaticModelsListText(const SithWorld* pWorld);
#endif

// Loading functions which reads NDY models section and loads models
int J3DAPI sithModel_ReadStaticModelsListText(SithWorld* pWorld, int bSkip);

// Loading functions which reads CND models section and loads models
int J3DAPI sithModel_WriteStaticModelsListBinary(tFileHandle fh, const SithWorld* pWorld);
int J3DAPI sithModel_ReadStaticModelsListBinary(tFileHandle fh, SithWorld* pWorld);

rdModel3* J3DAPI sithModel_Load(const char* pName, int bSkipDefault);

int J3DAPI sithModel_AllocWorldModels(SithWorld* pWorld, size_t size);
void J3DAPI sithModel_FreeWorldModels(SithWorld* pWorld);
size_t  J3DAPI sithModel_GetModelMemUsage(const rdModel3* pModel);

rdModel3* J3DAPI sithModel_GetModel(const char* pName);
rdModel3* J3DAPI sithModel_GetModelByIndex(int16_t modelIdx);
int16_t J3DAPI sithModel_GetModelIndex(const rdModel3* pModel);
int J3DAPI sithModel_GetMeshIndex(const rdModel3* pModel, const char* pMeshName);

void J3DAPI sithModel_EnableHiPoly(int bEnabled);

#ifdef J3D_PORT
// Port: Indy's outfit, the "Indy Outfit" setting (docs/PORTING.md, "Indy's outfit"). The values of the setting.
#define SITHMODEL_OUTFIT_GAME     0 // as the level chooses (retail)
#define SITHMODEL_OUTFIT_JACKET   1
#define SITHMODEL_OUTFIT_NOJACKET 2

// Latches the outfit for the next world load and for the models loaded by name while it is open. Called as a level
// opens (sithOpenNormal), so a change made in a menu takes effect at the next level start or restore.
void sithModel_SetOutfit(int outfit);
int sithModel_GetOutfit(void);

// After a restore, whose things come back with the models they had when the game was saved: gives each thing drawn
// with one of Indy's models the model its template has under the latched outfit (the level's own with the game's
// choice), so a save follows the outfit chosen when it is loaded.
void sithModel_ApplyOutfit(SithWorld* pWorld);
#endif

J3D_EXTERN_C_END
#endif // SITH_SITHMODEL_H
