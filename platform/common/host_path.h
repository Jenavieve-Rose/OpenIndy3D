#ifndef J3D_PLATFORM_HOST_PATH_H
#define J3D_PLATFORM_HOST_PATH_H
/*
 * The one place engine paths become host paths (PORT-PLAN.md Phase 1, "Filesystem and VFS").
 *
 * Engine paths come from scripts, level files and hard-coded format strings written for Windows: they may use '\' and
 * any letter case. J3DHost_ToPosixPath translates '\' to '/' and then, if the path does not exist as spelled, resolves
 * each component case-insensitively against the directory it is in, so "resource\Cog\foo.cog" finds RESOURCE/Cog/Foo.cog
 * on a case-sensitive volume. Components that do not exist in any case are kept as given, so the result is also the
 * right name to create.
 */
#include <stddef.h>

void J3DHost_ToPosixPath(char* pDst, size_t size, const char* pSrc);

#endif
