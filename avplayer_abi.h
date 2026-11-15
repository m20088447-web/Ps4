#pragma once

/*
 * Minimal SceAvPlayer ABI declarations used by PhoneCast.
 *
 * OpenOrbis v0.5.4 exposes AvPlayer NIDs, but its generated header currently
 * declares these functions without parameter types. These declarations follow
 * publicly used homebrew ABI layouts and must be validated on target hardware.
 * Do not include <orbis/AvPlayer.h> in this translation unit.
 */

#include <stdint.h>

extern "C" {

typedef struct SceAvPlayerImpl *SceAvPlayerHandle;

typedef void *(*SceAvPlayerAllocate)(void *arg, uint32_t alignment, uint32_t size);
typedef void (*SceAvPlayerDeallocate)(void *arg, void *ptr);
typedef void *(*SceAvPlayerAllocateTexture)(void *arg, uint32_t alignment, uint32_t size);
typedef void (*SceAvPlayerDeallocateTexture)(void *arg, void *ptr);
typedef int (*SceAvPlayerOpenFile)(void *arg, const char *filename);
typedef int (*SceAvPlayerCloseFile)(void *arg);
typedef int (*SceAvPlayerReadOffsetFile)(void *arg, uint8_t *buffer, uint64_t position, uint32_t length);
typedef uint64_t (*SceAvPlayerSizeFile)(void *arg);
typedef void (*SceAvPlayerEventCallback)(void *arg, int32_t eventId, int32_t sourceId, void *eventData);

typedef struct SceAvPlayerMemAllocator {
    void *objectPointer;
    SceAvPlayerAllocate allocate;
    SceAvPlayerDeallocate deallocate;
    SceAvPlayerAllocateTexture allocateTexture;
    SceAvPlayerDeallocateTexture deallocateTexture;
} SceAvPlayerMemAllocator;

typedef struct SceAvPlayerFileReplacement {
    void *objectPointer;
    SceAvPlayerOpenFile open;
    SceAvPlayerCloseFile close;
    SceAvPlayerReadOffsetFile readOffset;
    SceAvPlayerSizeFile size;
} SceAvPlayerFileReplacement;

typedef struct SceAvPlayerEventReplacement {
    void *objectPointer;
    SceAvPlayerEventCallback eventCallback;
} SceAvPlayerEventReplacement;

typedef struct SceAvPlayerInitData {
    SceAvPlayerMemAllocator memoryReplacement;
    SceAvPlayerFileReplacement fileReplacement;
    SceAvPlayerEventReplacement eventReplacement;
    int32_t debugLevel;
    uint32_t basePriority;
    int32_t numOutputVideoFrameBuffers;
    int32_t autoStart;
    uint8_t reserved[3];
    const char *defaultLanguage;
} SceAvPlayerInitData;

typedef struct SceAvPlayerVideoEx {
    uint32_t width;
    uint32_t height;
    float aspectRatio;
    uint8_t languageCode[4];
    uint32_t framerate;
    uint32_t cropLeftOffset;
    uint32_t cropRightOffset;
    uint32_t cropTopOffset;
    uint32_t cropBottomOffset;
    uint32_t pitch;
    uint8_t lumaBitDepth;
    uint8_t chromaBitDepth;
    uint8_t videoFullRangeFlag;
    uint8_t reserved1[37];
} SceAvPlayerVideoEx;

typedef union SceAvPlayerStreamDetailsEx {
    SceAvPlayerVideoEx video;
    uint8_t reserved1[80];
} SceAvPlayerStreamDetailsEx;

typedef struct SceAvPlayerFrameInfoEx {
    void *pData;
    uint8_t reserved[4];
    uint64_t timeStamp;
    SceAvPlayerStreamDetailsEx details;
} SceAvPlayerFrameInfoEx;

typedef struct SceAvPlayerUri {
    const char *name;
    uint32_t length;
} SceAvPlayerUri;

// Argument of sceAvPlayerAddSourceEx (128 bytes).
typedef struct SceAvPlayerSourceDetails {
    SceAvPlayerUri uri;
    uint8_t reserved1[64];
    int32_t sourceType;
    uint8_t reserved2[44];
} SceAvPlayerSourceDetails;

SceAvPlayerHandle sceAvPlayerInit(SceAvPlayerInitData *data);
// Weak: if a toolchain stub lacks it the pointer is null and callers skip it.
int32_t sceAvPlayerAddSource(SceAvPlayerHandle handle, const char *url) __attribute__((weak));
int32_t sceAvPlayerAddSourceEx(SceAvPlayerHandle handle, int32_t uriType, SceAvPlayerSourceDetails *details);
int32_t sceAvPlayerClose(SceAvPlayerHandle handle);
int32_t sceAvPlayerGetVideoDataEx(SceAvPlayerHandle handle, SceAvPlayerFrameInfoEx *frameInfo);
int32_t sceAvPlayerStop(SceAvPlayerHandle handle);
uint8_t sceAvPlayerIsActive(SceAvPlayerHandle handle);

}

#define PHONECAST_AVPLAYER_DEBUG_NONE 0
#define PHONECAST_AVPLAYER_URI_TYPE_SOURCE 0
#define PHONECAST_AVPLAYER_SOURCE_HLS 8

