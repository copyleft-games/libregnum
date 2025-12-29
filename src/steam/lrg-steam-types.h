/* lrg-steam-types.h - Pure C declarations for Steam API types
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * This header provides pure C declarations for the Steam flat API types
 * and functions. It does NOT include the Steam SDK headers, which require
 * C++. Instead, it declares only the types and functions needed by
 * Libregnum's Steam wrappers.
 *
 * At link time, these declarations resolve against the Steam library
 * (libsteam_api.so on Linux, steam_api64.dll on Windows).
 */

#ifndef LRG_STEAM_TYPES_H
#define LRG_STEAM_TYPES_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

G_BEGIN_DECLS

#ifdef LRG_ENABLE_STEAM

/*
 * Steam API types
 * These are forward declarations matching the Steam SDK types.
 */

/* Opaque interface pointers */
typedef struct ISteamUser_t      *ISteamUser;
typedef struct ISteamFriends_t   *ISteamFriends;
typedef struct ISteamUtils_t     *ISteamUtils;
typedef struct ISteamUserStats_t *ISteamUserStats;
typedef struct ISteamRemoteStorage_t *ISteamRemoteStorage;
typedef struct ISteamInput_t     *ISteamInput;

/* Steam ID type (64-bit unsigned) */
typedef uint64_t SteamID;

/* Steam API call handle */
typedef uint64_t SteamAPICall_t;

/* Application ID type */
typedef uint32_t AppId_t;

/* Initialization result enum */
typedef enum
{
    k_ESteamAPIInitResult_OK = 0,
    k_ESteamAPIInitResult_FailedGeneric = 1,
    k_ESteamAPIInitResult_NoSteamClient = 2,
    k_ESteamAPIInitResult_VersionMismatch = 3
} ESteamAPIInitResult;

/* Error message buffer */
typedef char SteamErrMsg[1024];

/*
 * Steam API function declarations
 * These match the signatures in steam_api_flat.h
 */

/* Core initialization/shutdown */
G_GNUC_INTERNAL ESteamAPIInitResult SteamAPI_InitFlat (SteamErrMsg *pOutErrMsg);
G_GNUC_INTERNAL void SteamAPI_Shutdown (void);
G_GNUC_INTERNAL void SteamAPI_RunCallbacks (void);

/* Interface accessors */
G_GNUC_INTERNAL ISteamUser        *SteamAPI_SteamUser_v023 (void);
G_GNUC_INTERNAL ISteamFriends     *SteamAPI_SteamFriends_v018 (void);
G_GNUC_INTERNAL ISteamUtils       *SteamAPI_SteamUtils_v010 (void);
G_GNUC_INTERNAL ISteamUserStats   *SteamAPI_SteamUserStats_v013 (void);
G_GNUC_INTERNAL ISteamRemoteStorage *SteamAPI_SteamRemoteStorage_v016 (void);

/* ISteamUser functions */
G_GNUC_INTERNAL bool     SteamAPI_ISteamUser_BLoggedOn (ISteamUser *self);
G_GNUC_INTERNAL SteamID  SteamAPI_ISteamUser_GetSteamID (ISteamUser *self);

/* ISteamFriends functions */
G_GNUC_INTERNAL const char *SteamAPI_ISteamFriends_GetPersonaName (ISteamFriends *self);
G_GNUC_INTERNAL bool SteamAPI_ISteamFriends_SetRichPresence (ISteamFriends *self,
                                                             const char *pchKey,
                                                             const char *pchValue);
G_GNUC_INTERNAL void SteamAPI_ISteamFriends_ClearRichPresence (ISteamFriends *self);

/* ISteamUtils functions */
G_GNUC_INTERNAL uint32_t SteamAPI_ISteamUtils_GetAppID (ISteamUtils *self);

/* ISteamUserStats functions (achievements and stats) */
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_RequestCurrentStats (ISteamUserStats *self);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_GetAchievement (ISteamUserStats *self,
                                                              const char *pchName,
                                                              bool *pbAchieved);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_SetAchievement (ISteamUserStats *self,
                                                              const char *pchName);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_ClearAchievement (ISteamUserStats *self,
                                                                const char *pchName);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_StoreStats (ISteamUserStats *self);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_GetStatInt32 (ISteamUserStats *self,
                                                            const char *pchName,
                                                            int32_t *pData);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_GetStatFloat (ISteamUserStats *self,
                                                            const char *pchName,
                                                            float *pData);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_SetStatInt32 (ISteamUserStats *self,
                                                            const char *pchName,
                                                            int32_t nData);
G_GNUC_INTERNAL bool SteamAPI_ISteamUserStats_SetStatFloat (ISteamUserStats *self,
                                                            const char *pchName,
                                                            float fData);
G_GNUC_INTERNAL uint32_t SteamAPI_ISteamUserStats_GetNumAchievements (ISteamUserStats *self);
G_GNUC_INTERNAL const char *SteamAPI_ISteamUserStats_GetAchievementName (ISteamUserStats *self,
                                                                          uint32_t iAchievement);

/* ISteamRemoteStorage functions (cloud saves) */
G_GNUC_INTERNAL bool SteamAPI_ISteamRemoteStorage_FileWrite (ISteamRemoteStorage *self,
                                                             const char *pchFile,
                                                             const void *pvData,
                                                             int32_t cubData);
G_GNUC_INTERNAL int32_t SteamAPI_ISteamRemoteStorage_FileRead (ISteamRemoteStorage *self,
                                                                const char *pchFile,
                                                                void *pvData,
                                                                int32_t cubDataToRead);
G_GNUC_INTERNAL bool SteamAPI_ISteamRemoteStorage_FileDelete (ISteamRemoteStorage *self,
                                                              const char *pchFile);
G_GNUC_INTERNAL bool SteamAPI_ISteamRemoteStorage_FileExists (ISteamRemoteStorage *self,
                                                              const char *pchFile);
G_GNUC_INTERNAL int32_t SteamAPI_ISteamRemoteStorage_GetFileSize (ISteamRemoteStorage *self,
                                                                   const char *pchFile);
G_GNUC_INTERNAL int32_t SteamAPI_ISteamRemoteStorage_GetFileCount (ISteamRemoteStorage *self);
G_GNUC_INTERNAL const char *SteamAPI_ISteamRemoteStorage_GetFileNameAndSize (ISteamRemoteStorage *self,
                                                                              int iFile,
                                                                              int32_t *pnFileSizeInBytes);
G_GNUC_INTERNAL bool SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount (ISteamRemoteStorage *self);
G_GNUC_INTERNAL bool SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp (ISteamRemoteStorage *self);

#endif /* LRG_ENABLE_STEAM */

G_END_DECLS

#endif /* LRG_STEAM_TYPES_H */
