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
typedef struct ISteamUGC_t       *ISteamUGC;

/* Steam ID type (64-bit unsigned) */
typedef uint64_t SteamID;

/* Steam API call handle */
typedef uint64_t SteamAPICall_t;

/* Application ID type */
typedef uint32_t AppId_t;

/* UGC (Workshop) types */
typedef uint64_t PublishedFileId_t;
typedef uint64_t UGCQueryHandle_t;
typedef uint64_t UGCUpdateHandle_t;
typedef uint64_t UGCHandle_t;

/* UGC item state flags */
typedef enum
{
    k_EItemStateNone            = 0,
    k_EItemStateSubscribed      = 1,
    k_EItemStateLegacyItem      = 2,
    k_EItemStateInstalled       = 4,
    k_EItemStateNeedsUpdate     = 8,
    k_EItemStateDownloading     = 16,
    k_EItemStateDownloadPending = 32
} EItemState;

/* UGC query types */
typedef enum
{
    k_EUGCQuery_RankedByVote                                  = 0,
    k_EUGCQuery_RankedByPublicationDate                       = 1,
    k_EUGCQuery_AcceptedForGameRankedByAcceptanceDate         = 2,
    k_EUGCQuery_RankedByTrend                                 = 3,
    k_EUGCQuery_FavoritedByFriendsRankedByPublicationDate     = 4,
    k_EUGCQuery_CreatedByFriendsRankedByPublicationDate       = 5,
    k_EUGCQuery_RankedByNumTimesReported                      = 6,
    k_EUGCQuery_CreatedByFollowedUsersRankedByPublicationDate = 7,
    k_EUGCQuery_NotYetRated                                   = 8,
    k_EUGCQuery_RankedByTotalVotesAsc                         = 9,
    k_EUGCQuery_RankedByVotesUp                               = 10,
    k_EUGCQuery_RankedByTextSearch                            = 11,
    k_EUGCQuery_RankedByTotalUniqueSubscriptions              = 12,
    k_EUGCQuery_RankedByPlaytimeTrend                         = 13,
    k_EUGCQuery_RankedByTotalPlaytime                         = 14,
    k_EUGCQuery_RankedByAveragePlaytimeTrend                  = 15,
    k_EUGCQuery_RankedByLifetimeAveragePlaytime               = 16,
    k_EUGCQuery_RankedByPlaytimeSessionsTrend                 = 17,
    k_EUGCQuery_RankedByLifetimePlaytimeSessions              = 18,
    k_EUGCQuery_RankedByLastUpdatedDate                       = 19
} EUGCQuery;

/* UGC matching type */
typedef enum
{
    k_EUGCMatchingUGCType_Items              = 0,
    k_EUGCMatchingUGCType_Items_Mtx          = 1,
    k_EUGCMatchingUGCType_Items_ReadyToUse   = 2,
    k_EUGCMatchingUGCType_Collections        = 3,
    k_EUGCMatchingUGCType_Artwork            = 4,
    k_EUGCMatchingUGCType_Videos             = 5,
    k_EUGCMatchingUGCType_Screenshots        = 6,
    k_EUGCMatchingUGCType_AllGuides          = 7,
    k_EUGCMatchingUGCType_WebGuides          = 8,
    k_EUGCMatchingUGCType_IntegratedGuides   = 9,
    k_EUGCMatchingUGCType_UsableInGame       = 10,
    k_EUGCMatchingUGCType_ControllerBindings = 11,
    k_EUGCMatchingUGCType_GameManagedItems   = 12,
    k_EUGCMatchingUGCType_All                = ~0
} EUGCMatchingUGCType;

/* UGC update result */
typedef enum
{
    k_EResultOK                 = 1,
    k_EResultFail               = 2,
    k_EResultInvalidParam       = 8,
    k_EResultAccessDenied       = 15,
    k_EResultTimeout            = 16,
    k_EResultFileNotFound       = 9,
    k_EResultLimitExceeded      = 25,
    k_EResultDuplicateRequest   = 29,
    k_EResultInsufficientPrivilege = 24
} EResult;

/* UGC file type */
typedef enum
{
    k_EWorkshopFileTypeCommunity              = 0,
    k_EWorkshopFileTypeMicrotransaction       = 1,
    k_EWorkshopFileTypeCollection             = 2,
    k_EWorkshopFileTypeArt                    = 3,
    k_EWorkshopFileTypeVideo                  = 4,
    k_EWorkshopFileTypeScreenshot             = 5,
    k_EWorkshopFileTypeGame                   = 6,
    k_EWorkshopFileTypeSoftware               = 7,
    k_EWorkshopFileTypeConcept                = 8,
    k_EWorkshopFileTypeWebGuide               = 9,
    k_EWorkshopFileTypeIntegratedGuide        = 10,
    k_EWorkshopFileTypeMerch                  = 11,
    k_EWorkshopFileTypeControllerBinding      = 12,
    k_EWorkshopFileTypeSteamworksAccessInvite = 13,
    k_EWorkshopFileTypeSteamVideo             = 14,
    k_EWorkshopFileTypeGameManagedItem        = 15
} EWorkshopFileType;

/* UGC item visibility */
typedef enum
{
    k_ERemoteStoragePublishedFileVisibilityPublic      = 0,
    k_ERemoteStoragePublishedFileVisibilityFriendsOnly = 1,
    k_ERemoteStoragePublishedFileVisibilityPrivate     = 2,
    k_ERemoteStoragePublishedFileVisibilityUnlisted    = 3
} ERemoteStoragePublishedFileVisibility;

/* Query details structure (subset of SteamUGCDetails_t) */
typedef struct
{
    PublishedFileId_t m_nPublishedFileId;
    EResult           m_eResult;
    EWorkshopFileType m_eFileType;
    AppId_t           m_nCreatorAppID;
    AppId_t           m_nConsumerAppID;
    char              m_rgchTitle[129];
    char              m_rgchDescription[8000];
    uint64_t          m_ulSteamIDOwner;
    uint32_t          m_rtimeCreated;
    uint32_t          m_rtimeUpdated;
    ERemoteStoragePublishedFileVisibility m_eVisibility;
    bool              m_bBanned;
    bool              m_bAcceptedForUse;
    bool              m_bTagsTruncated;
    char              m_rgchTags[1025];
    UGCHandle_t       m_hFile;
    UGCHandle_t       m_hPreviewFile;
    char              m_pchFileName[260];
    int32_t           m_nFileSize;
    int32_t           m_nPreviewFileSize;
    char              m_rgchURL[256];
    uint32_t          m_unVotesUp;
    uint32_t          m_unVotesDown;
    float             m_flScore;
    uint32_t          m_unNumChildren;
} SteamUGCDetails_t;

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

/* Interface accessor for UGC */
G_GNUC_INTERNAL ISteamUGC *SteamAPI_SteamUGC_v018 (void);

/* ISteamUGC functions (Workshop) */
G_GNUC_INTERNAL UGCQueryHandle_t SteamAPI_ISteamUGC_CreateQueryUserUGCRequest (
    ISteamUGC *self,
    uint32_t unAccountID,
    int eListType,
    int eMatchingUGCType,
    int eSortOrder,
    AppId_t nCreatorAppID,
    AppId_t nConsumerAppID,
    uint32_t unPage);

G_GNUC_INTERNAL UGCQueryHandle_t SteamAPI_ISteamUGC_CreateQueryAllUGCRequestPage (
    ISteamUGC *self,
    int eQueryType,
    int eMatchingUGCType,
    AppId_t nCreatorAppID,
    AppId_t nConsumerAppID,
    uint32_t unPage);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_SetSearchText (ISteamUGC *self,
                                                       UGCQueryHandle_t handle,
                                                       const char *pSearchText);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_AddRequiredTag (ISteamUGC *self,
                                                        UGCQueryHandle_t handle,
                                                        const char *pTagName);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_AddExcludedTag (ISteamUGC *self,
                                                        UGCQueryHandle_t handle,
                                                        const char *pTagName);

G_GNUC_INTERNAL SteamAPICall_t SteamAPI_ISteamUGC_SendQueryUGCRequest (ISteamUGC *self,
                                                                        UGCQueryHandle_t handle);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_GetQueryUGCResult (ISteamUGC *self,
                                                           UGCQueryHandle_t handle,
                                                           uint32_t index,
                                                           SteamUGCDetails_t *pDetails);

G_GNUC_INTERNAL uint32_t SteamAPI_ISteamUGC_GetQueryUGCNumTags (ISteamUGC *self,
                                                                 UGCQueryHandle_t handle,
                                                                 uint32_t index);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_GetQueryUGCTag (ISteamUGC *self,
                                                        UGCQueryHandle_t handle,
                                                        uint32_t index,
                                                        uint32_t tagIndex,
                                                        char *pchValue,
                                                        uint32_t cchValueSize);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_ReleaseQueryUGCRequest (ISteamUGC *self,
                                                                 UGCQueryHandle_t handle);

G_GNUC_INTERNAL uint32_t SteamAPI_ISteamUGC_GetSubscribedItems (ISteamUGC *self,
                                                                 PublishedFileId_t *pvecPublishedFileID,
                                                                 uint32_t cMaxEntries);

G_GNUC_INTERNAL uint32_t SteamAPI_ISteamUGC_GetNumSubscribedItems (ISteamUGC *self);

G_GNUC_INTERNAL uint32_t SteamAPI_ISteamUGC_GetItemState (ISteamUGC *self,
                                                          PublishedFileId_t nPublishedFileID);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_GetItemInstallInfo (ISteamUGC *self,
                                                            PublishedFileId_t nPublishedFileID,
                                                            uint64_t *punSizeOnDisk,
                                                            char *pchFolder,
                                                            uint32_t cchFolderSize,
                                                            uint32_t *punTimeStamp);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_GetItemDownloadInfo (ISteamUGC *self,
                                                              PublishedFileId_t nPublishedFileID,
                                                              uint64_t *punBytesDownloaded,
                                                              uint64_t *punBytesTotal);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_DownloadItem (ISteamUGC *self,
                                                       PublishedFileId_t nPublishedFileID,
                                                       bool bHighPriority);

G_GNUC_INTERNAL SteamAPICall_t SteamAPI_ISteamUGC_SubscribeItem (ISteamUGC *self,
                                                                  PublishedFileId_t nPublishedFileID);

G_GNUC_INTERNAL SteamAPICall_t SteamAPI_ISteamUGC_UnsubscribeItem (ISteamUGC *self,
                                                                    PublishedFileId_t nPublishedFileID);

G_GNUC_INTERNAL UGCUpdateHandle_t SteamAPI_ISteamUGC_StartItemUpdate (ISteamUGC *self,
                                                                       AppId_t nConsumerAppID,
                                                                       PublishedFileId_t nPublishedFileID);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_SetItemTitle (ISteamUGC *self,
                                                       UGCUpdateHandle_t handle,
                                                       const char *pchTitle);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_SetItemDescription (ISteamUGC *self,
                                                             UGCUpdateHandle_t handle,
                                                             const char *pchDescription);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_SetItemVisibility (ISteamUGC *self,
                                                            UGCUpdateHandle_t handle,
                                                            int eVisibility);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_SetItemTags (ISteamUGC *self,
                                                      UGCUpdateHandle_t handle,
                                                      const void *pTags);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_SetItemContent (ISteamUGC *self,
                                                         UGCUpdateHandle_t handle,
                                                         const char *pszContentFolder);

G_GNUC_INTERNAL bool SteamAPI_ISteamUGC_SetItemPreview (ISteamUGC *self,
                                                         UGCUpdateHandle_t handle,
                                                         const char *pszPreviewFile);

G_GNUC_INTERNAL SteamAPICall_t SteamAPI_ISteamUGC_SubmitItemUpdate (ISteamUGC *self,
                                                                     UGCUpdateHandle_t handle,
                                                                     const char *pchChangeNote);

G_GNUC_INTERNAL int SteamAPI_ISteamUGC_GetItemUpdateProgress (ISteamUGC *self,
                                                               UGCUpdateHandle_t handle,
                                                               uint64_t *punBytesProcessed,
                                                               uint64_t *punBytesTotal);

G_GNUC_INTERNAL SteamAPICall_t SteamAPI_ISteamUGC_CreateItem (ISteamUGC *self,
                                                               AppId_t nConsumerAppID,
                                                               int eFileType);

G_GNUC_INTERNAL SteamAPICall_t SteamAPI_ISteamUGC_DeleteItem (ISteamUGC *self,
                                                               PublishedFileId_t nPublishedFileID);

#endif /* LRG_ENABLE_STEAM */

G_END_DECLS

#endif /* LRG_STEAM_TYPES_H */
