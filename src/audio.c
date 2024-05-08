#include "audio.h"

#include <stdio.h>
#include <unistd.h>

#define HALF(x) ((x) / 2)

typedef uint8_t Bool8;

#define RIFF_MAGIC (uint8_t[4]){'R', 'I', 'F', 'F'}
#define WAVE_MAGIC (uint8_t[4]){'W', 'A', 'V', 'E'}
#define FMT_MAGIC  (uint8_t[4]){'f', 'm', 't', ' '}
#define DATA_MAGIC (uint8_t[4]){'d', 'a', 't', 'a'}
#define FMT_CHUNK_SIZE (16)

#define PCM_FORMAT (1)
#define PCM_BLOCK_MODE (0)
#define PCM_SEARCH_DIRECTION_NEAR (0)

#define MICROSECONDS_PER_MILLISECOND (1000)
#define MILLISECONDS_PER_SECOND (1000)
#define BITS_PER_BYTE (8)
#define MAGIC_SIZE (4)

#define BUFFER_SIZE_FACTOR (8)
#define INTERNAL_BARRIER_COUNT (2)

typedef struct __attribute__((packed)) {
    uint8_t riffMagic[4];
    uint32_t fileSize;
    uint8_t waveMagic[4];
} AudioRiffHeader;

typedef struct __attribute__((packed)) {
    uint8_t fmtMagic[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
} AudioFmtChunk;

typedef struct __attribute__((packed)) {
    uint8_t dataMagic[4];
    uint32_t dataSize;
} AudioDataChunk;

typedef struct {
    uint32_t sampleRate;
    uint32_t byteRate;
    uint32_t dataSize;
    uint32_t audioLength;  // in milliseconds
    uint16_t channelAmount;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint8_t *data;
    uint8_t __align;
} AudioRiffData;

typedef struct {
    AudioRiffData riffData;
    snd_pcm_t *pcmHandle;
    pthread_t *thread;
    pthread_barrier_t *externalBarrier;
    pthread_barrier_t *internalBarrier;
    pthread_mutex_t *actionLock;
    AudioError *error;
    char *soundDeviceName;
    uint32_t currentFrame;
    uint32_t lastFrame;
    uint32_t timeResolution; 
    uint32_t alsaBufferSize;
    uint32_t jumpTarget;  // in milliseconds
    Bool8 soundDeviceNameSet;
    Bool8 useExternalBarrier;
    Bool8 isPlaying;
    Bool8 isPaused;
    Bool8 playFlag;
    Bool8 pauseFlag;
    Bool8 stopFlag;
    Bool8 haltFlag;
    Bool8 jumpFlag;
    uint8_t __align[3];
} _AudioObject;

void _waitForBarriers(_AudioObject *_self) {
    if (_self->externalBarrier != NULL) {
        pthread_barrier_wait(_self->externalBarrier);
        _self->externalBarrier = NULL;
    }
    pthread_barrier_wait(_self->internalBarrier);
}

void _play(_AudioObject *_self) {
    _self->playFlag = false;
    _self->isPlaying = true;
    _self->isPaused = false;
}

void _pause(_AudioObject *_self) {
    _self->pauseFlag = false;
    _self->isPlaying = false;
    _self->isPaused = true;
    snd_pcm_sframes_t delay;
    snd_pcm_delay(_self->pcmHandle, &delay);
    _self->currentFrame -= delay;
    if (_self->currentFrame < 0) _self->currentFrame = 0;
    snd_pcm_drop(_self->pcmHandle);
    snd_pcm_prepare(_self->pcmHandle);
}

void _stop(_AudioObject *_self) {
    _self->stopFlag = false;
    _self->isPlaying = false;
    _self->isPaused = true;
    _self->currentFrame = 0;
    snd_pcm_drop(_self->pcmHandle);
    snd_pcm_prepare(_self->pcmHandle);
}

void _jump(_AudioObject *_self) {
    _self->jumpFlag = false;
    _self->currentFrame = _self->jumpTarget 
        * _self->riffData.byteRate 
        / MICROSECONDS_PER_MILLISECOND;
    if (_self->currentFrame > _self->lastFrame) {
        _self->currentFrame = _self->lastFrame;
    }
    snd_pcm_drop(_self->pcmHandle);
    snd_pcm_prepare(_self->pcmHandle);
}

snd_pcm_uframes_t _getFramesAvailable(_AudioObject *_self) {
    snd_pcm_status_t *status;
    snd_pcm_status_malloc(&status);
    snd_pcm_status(_self->pcmHandle, status);
    snd_pcm_uframes_t framesAvailable = snd_pcm_status_get_avail(status);
    snd_pcm_status_free(status);
    return framesAvailable;
}

snd_pcm_uframes_t _getFramesToWrite(
    _AudioObject *_self, snd_pcm_uframes_t framesAvailable, bool *endReached
) {
    snd_pcm_uframes_t framesToWrite = framesAvailable;
    *endReached = false;
    if (_self->currentFrame + framesToWrite > _self->lastFrame) {
        framesToWrite = _self->lastFrame - _self->currentFrame;
        *endReached = true;
    }
    return framesToWrite;
}

void * _mainloop(void *self) {
    _AudioObject *_self = (_AudioObject*)self;
    _self->isPaused = true;

    while (!_self->haltFlag) {
        if (_self->playFlag) {
            _play(_self);
            _waitForBarriers(_self);
        } else if (_self->pauseFlag) {
            _pause(_self);
            _waitForBarriers(_self);
        } else if (_self->stopFlag) {
            _stop(_self);
            _waitForBarriers(_self);
        } else if (_self->jumpFlag) {
            _jump(_self);
            _waitForBarriers(_self);
        }

        usleep(_self->timeResolution * MICROSECONDS_PER_MILLISECOND);
        if (_self->isPaused) continue;

        snd_pcm_uframes_t framesAvailable = _getFramesAvailable(_self);

        if (framesAvailable > HALF(_self->alsaBufferSize)) {
            bool endReached = false;
            snd_pcm_uframes_t framesToWrite = _getFramesToWrite(
                _self, framesAvailable, &endReached
            );

            size_t pcm_offset = _self->currentFrame 
                * _self->riffData.blockAlign;

            if (snd_pcm_writei(
                _self->pcmHandle, 
                (void*)(_self->riffData.data + pcm_offset), 
                framesToWrite
            ) == -EPIPE) {
                snd_pcm_prepare(_self->pcmHandle);
            }

            if (endReached) {
                _stop(_self);
            } else {
                _self->currentFrame += framesToWrite;
            }
        }
    }

    pthread_exit(NULL);
    return NULL;
}

bool _readRiffFile(_AudioObject *_self, void *rawData, size_t rawDataSize) {
    if (
        rawDataSize < sizeof(AudioRiffHeader) 
        + sizeof(AudioFmtChunk) 
        + sizeof(AudioDataChunk)
    ) {
        _self->error->type = AUDIO_ERROR_FILE_TOO_SMALL;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }

    // check RIFF header
    AudioRiffHeader *riffHeader = (AudioRiffHeader*)rawData;
    if (memcmp(riffHeader->riffMagic, RIFF_MAGIC, MAGIC_SIZE)) {
        _self->error->type = AUDIO_ERROR_INVALID_RIFF_MAGIC_NUMBER;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    if (memcmp(riffHeader->waveMagic, WAVE_MAGIC, MAGIC_SIZE)) {
        _self->error->type = AUDIO_ERROR_INVALID_WAVE_MAGIC_NUMBER;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    if (
        riffHeader->fileSize 
        != rawDataSize - (riffHeader->waveMagic - (uint8_t*)riffHeader)
    ) {
        _self->error->type = AUDIO_ERROR_INVALID_FILE_SIZE;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }

    // check and read fmt chunk
    AudioFmtChunk *fmtChunk = (AudioFmtChunk*)(
        (uint8_t*)rawData + sizeof(AudioRiffHeader)
    );
    if (memcmp(fmtChunk->fmtMagic, FMT_MAGIC, MAGIC_SIZE)) {
        _self->error->type = AUDIO_ERROR_IMVALID_FMT_MAGIC_NUMBER;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    if (fmtChunk->fmtSize != FMT_CHUNK_SIZE) {
        _self->error->type = AUDIO_ERROR_INVALID_FMT_SIZE;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    if (fmtChunk->audioFormat != PCM_FORMAT) {
        _self->error->type = AUDIO_ERROR_NO_PCM_FORMAT;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    _self->riffData.channelAmount = fmtChunk->numChannels;
    _self->riffData.channelAmount = fmtChunk->numChannels;
    _self->riffData.sampleRate = fmtChunk->sampleRate;
    _self->riffData.byteRate = fmtChunk->byteRate;
    _self->riffData.blockAlign = fmtChunk->blockAlign;
    _self->riffData.bitsPerSample = fmtChunk->bitsPerSample;
    if (
        _self->riffData.byteRate 
        != _self->riffData.sampleRate 
            * _self->riffData.channelAmount 
            * _self->riffData.bitsPerSample / BITS_PER_BYTE
    ) {
        _self->error->type = AUDIO_ERROR_INVALID_BYTE_RATE;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    if (
        _self->riffData.blockAlign 
        != _self->riffData.channelAmount 
        * _self->riffData.bitsPerSample / BITS_PER_BYTE
    ) {
        _self->error->type = AUDIO_ERROR_INVALID_BLOCK_ALIGN;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }

    // look for begin of data chunk
    size_t dataChunkOffset = sizeof(AudioRiffHeader) + sizeof(AudioFmtChunk);
    while (memcmp(
        (uint8_t*)rawData + dataChunkOffset, DATA_MAGIC, MAGIC_SIZE
    )) {
        dataChunkOffset++;
        if (dataChunkOffset >= rawDataSize) {
            _self->error->type = AUDIO_ERROR_DATA_CHUNK_NOT_FOUND;
            _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
            return false;
        }
    }

    // check and read data chunk
    AudioDataChunk *dataChunk = (AudioDataChunk*)(
        (uint8_t*)rawData + dataChunkOffset
    );
    if (memcmp(dataChunk->dataMagic, DATA_MAGIC, MAGIC_SIZE)) {
        _self->error->type = AUDIO_ERROR_INVALID_DATA_MAGIC_NUMBER;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    _self->riffData.dataSize = dataChunk->dataSize;
    if (
        _self->riffData.dataSize 
        != rawDataSize 
            - dataChunkOffset
            - sizeof(AudioDataChunk)
        ) {
        _self->error->type = AUDIO_ERROR_INVALID_DATA_SIZE;
        _self->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return false;
    }
    _self->riffData.data = (uint8_t*)rawData 
        + sizeof(AudioRiffHeader) 
        + sizeof(AudioFmtChunk) 
        + dataChunkOffset
        + sizeof(AudioDataChunk);

    _self->riffData.audioLength = _self->riffData.dataSize 
        * MICROSECONDS_PER_MILLISECOND 
        / _self->riffData.byteRate;

    return true;
}

void _initializeAudioObject(_AudioObject *_self) {
    _self->riffData.sampleRate = 0;
    _self->riffData.byteRate = 0;
    _self->riffData.dataSize = 0;
    _self->riffData.audioLength = 0;
    _self->riffData.channelAmount = 0;
    _self->riffData.blockAlign = 0;
    _self->riffData.bitsPerSample = 0;
    _self->riffData.data = NULL;
    _self->pcmHandle = NULL;
    _self->thread = NULL;
    _self->externalBarrier = NULL;
    _self->internalBarrier = NULL;
    _self->actionLock = NULL;
    _self->error = NULL;
    _self->soundDeviceName = NULL;
    _self->currentFrame = 0;
    _self->lastFrame = 0;
    _self->timeResolution = 0;
    _self->alsaBufferSize = 0;
    _self->jumpTarget = 0;
    _self->soundDeviceNameSet = false;
    _self->useExternalBarrier = false;
    _self->isPlaying = false;
    _self->isPaused = false;
    _self->playFlag = false;
    _self->pauseFlag = false;
    _self->stopFlag = false;
    _self->haltFlag = false;
    _self->jumpFlag = false;
}

bool _setSoundDeviceName(
    _AudioObject *audioObject, AudioConfiguration *configuration
) {
    if (configuration->soundDeviceName == NULL) {
        audioObject->soundDeviceName = "default";
        audioObject->soundDeviceNameSet = false;
    } else {
        audioObject->soundDeviceName = (char*)malloc(
            configuration->soundDeviceNameSize + 1
        );
        if (audioObject->soundDeviceName == NULL) {
            audioObject->error->type = AUDIO_ERROR_MEMORY_ALLOCATION_FAILED;
            audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
            return false;
        }
        memcpy(
            audioObject->soundDeviceName, 
            configuration->soundDeviceName, 
            configuration->soundDeviceNameSize
        );
        audioObject->soundDeviceName[
            configuration->soundDeviceNameSize
        ] = '\0';
        audioObject->soundDeviceNameSet = true;
    }
    return true;
}

AudioObject * audioInit(AudioConfiguration *configuration) {
    _AudioObject *audioObject = (_AudioObject*)malloc(sizeof(_AudioObject));
    if (audioObject == NULL) { return NULL; }
    
    _initializeAudioObject(audioObject);

    audioObject->error = (AudioError*)malloc(sizeof(AudioError));
    if (audioObject->error == NULL) { 
        free(audioObject);
        return NULL; 
    }
    audioObject->error->type = AUDIO_ERROR_NO_ERROR;
    audioObject->error->level = AUDIO_ERROR_LEVEL_INFO;
    audioObject->error->alsaErrorNumber = 0;

    if (!_readRiffFile(
        audioObject, configuration->rawData, configuration->rawDataSize
    )) {
        return (AudioObject*)audioObject;
    }

    if (!_setSoundDeviceName(audioObject, configuration)) {
        return (AudioObject*)audioObject;
    }

    if ((audioObject->error->alsaErrorNumber = snd_pcm_open(
        &audioObject->pcmHandle, 
        audioObject->soundDeviceName, 
        SND_PCM_STREAM_PLAYBACK, 
        PCM_BLOCK_MODE
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    snd_pcm_hw_params_t *hardwareParameters;
    snd_pcm_hw_params_alloca(&hardwareParameters);

    if ((audioObject->error->alsaErrorNumber = snd_pcm_hw_params_any(
        audioObject->pcmHandle, hardwareParameters
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    if ((audioObject->error->alsaErrorNumber = snd_pcm_hw_params_set_access(
        audioObject->pcmHandle, 
        hardwareParameters, 
        SND_PCM_ACCESS_RW_INTERLEAVED
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    snd_pcm_format_t format;
    switch (audioObject->riffData.bitsPerSample) {
        case 8:  format = SND_PCM_FORMAT_S8;         break;
        case 16: format = SND_PCM_FORMAT_S16_LE;     break;
        case 24: format = SND_PCM_FORMAT_S24_3LE;    break;
        case 32: format = SND_PCM_FORMAT_S32_LE;     break;
        case 64: format = SND_PCM_FORMAT_FLOAT64_LE; break;
        default:
            audioObject->error->type = AUDIO_UNSUPPORTED_BITS_PER_SAMPLE;
            audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
            return (AudioObject*)audioObject;
    }
    if ((audioObject->error->alsaErrorNumber = snd_pcm_hw_params_set_format(
        audioObject->pcmHandle, hardwareParameters, format
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    if ((audioObject->error->alsaErrorNumber = snd_pcm_hw_params_set_channels(
        audioObject->pcmHandle, 
        hardwareParameters, 
        audioObject->riffData.channelAmount
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    if ((audioObject->error->alsaErrorNumber = snd_pcm_hw_params_set_rate(
        audioObject->pcmHandle, 
        hardwareParameters, 
        audioObject->riffData.sampleRate, 
        PCM_SEARCH_DIRECTION_NEAR
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    snd_pcm_uframes_t bufferSizeInSamples = audioObject->riffData.sampleRate 
        * BUFFER_SIZE_FACTOR
        * configuration->timeResolution
        / MILLISECONDS_PER_SECOND;
    audioObject->alsaBufferSize = bufferSizeInSamples;
    if ((audioObject->error->alsaErrorNumber = snd_pcm_hw_params_set_buffer_size(
        audioObject->pcmHandle, hardwareParameters, bufferSizeInSamples
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    if ((audioObject->error->alsaErrorNumber = snd_pcm_hw_params(
        audioObject->pcmHandle, hardwareParameters
    )) < 0) {
        audioObject->error->type = AUDIO_ERROR_ALSA_ERROR;
        audioObject->error->level = AUDIO_ERROR_LEVEL_ERROR;
        return (AudioObject*)audioObject;
    }

    audioObject->timeResolution = configuration->timeResolution;
    audioObject->currentFrame = 0;
    audioObject->lastFrame = audioObject->riffData.dataSize 
        / audioObject->riffData.blockAlign;

    audioObject->thread = (pthread_t*)malloc(sizeof(pthread_t));
    audioObject->externalBarrier = NULL;
    audioObject->internalBarrier = (pthread_barrier_t*)malloc(
        sizeof(pthread_barrier_t)
    );
    audioObject->actionLock = (pthread_mutex_t*)malloc(
        sizeof(pthread_mutex_t)
    );
    pthread_mutex_init(audioObject->actionLock, NULL);

    audioObject->isPlaying = false;
    audioObject->isPaused = false;

    audioObject->playFlag = false;
    audioObject->pauseFlag = false;
    audioObject->stopFlag = false;
    audioObject->haltFlag = false;
    audioObject->jumpFlag = false;
    audioObject->jumpTarget = 0;

    pthread_create(audioObject->thread, NULL, _mainloop, (void*)audioObject);
    return (AudioObject)audioObject;
}

void audioDestroy(AudioObject *self) {
    _AudioObject *_self = (_AudioObject*)self;

    _self->haltFlag = true;
    if (_self->thread) {
        pthread_join(*(_self->thread), NULL);
        free(_self->thread);
    }
    
    if (_self->pcmHandle) {
        snd_pcm_drop(_self->pcmHandle);
        snd_pcm_close(_self->pcmHandle);
    }

    if (_self->externalBarrier) {
        pthread_barrier_destroy(_self->externalBarrier);
        free(_self->externalBarrier);
    }
    if (_self->actionLock) {
        pthread_mutex_destroy(_self->actionLock);
        free(_self->actionLock);
    }

    if (_self->soundDeviceNameSet) free(_self->soundDeviceName);
    if (_self->error) free(_self->error);

    free(_self);
}

bool _lockAction(
    _AudioObject *_self, pthread_barrier_t *barrier, bool predicate
) {
    pthread_mutex_lock(_self->actionLock);
    if (!predicate) {
        pthread_mutex_unlock(_self->actionLock);
        return false;
    }
    _self->externalBarrier = barrier;
    pthread_barrier_init(
        _self->internalBarrier, NULL, INTERNAL_BARRIER_COUNT
    );
    return true;
}

void _unlockAction(_AudioObject *_self) {
    pthread_barrier_wait(_self->internalBarrier);
    pthread_barrier_destroy(_self->internalBarrier);
    pthread_mutex_unlock(_self->actionLock);
}

bool audioPlay(AudioObject *self, pthread_barrier_t *barrier) {
    _AudioObject *_self = (_AudioObject*)self;
    if (!_lockAction(
        _self, barrier, 
        !_self->isPlaying
    )) {
        _self->error->type = AUDIO_WARING_ALREADY_PLAYING;
        _self->error->level = AUDIO_ERROR_LEVEL_WARNING;
        return false;
    }

    _self->playFlag = true;

    _unlockAction(_self);
    return true;
}

bool audioPause(AudioObject *self, pthread_barrier_t *barrier) {
    _AudioObject *_self = (_AudioObject*)self;
    if (!_lockAction(
        _self, barrier, 
        _self->isPlaying
    )) {
        _self->error->type = AUDIO_WARNING_ALREADY_PAUSED;
        _self->error->level = AUDIO_ERROR_LEVEL_WARNING;
        return false;
    }

    _self->pauseFlag = true;

    _unlockAction(_self);
    return true;
}

void audioStop(AudioObject *self, pthread_barrier_t *barrier) {
    _AudioObject *_self = (_AudioObject*)self;
    _lockAction(_self, barrier, true);

    _self->stopFlag = true;

    _unlockAction(_self);
}

void audioJump(
    AudioObject *self, pthread_barrier_t *barrier, uint32_t milliseconds
) {
    _AudioObject *_self = (_AudioObject*)self;
    _lockAction(_self, barrier, true);

    _self->jumpFlag = true;
    if (milliseconds > _self->riffData.audioLength) {
        _self->error->type = AUDIO_WARNING_JUMPED_BEYOND_END;
        _self->error->level = AUDIO_ERROR_LEVEL_WARNING;
    }
    _self->jumpTarget = milliseconds;

    _unlockAction(_self);
}

bool audioGetIsPlaying(AudioObject *self) { 
    return ((_AudioObject*)self)->isPlaying; 
}

bool audioGetIsPaused(AudioObject *self) { 
    return ((_AudioObject*)self)->isPaused; 
}

uint32_t audioGetCurrentTime(AudioObject *self) {
    return ((_AudioObject*)self)->currentFrame 
        * MILLISECONDS_PER_SECOND
        / ((_AudioObject*)self)->riffData.byteRate;
}

uint32_t audioGetTotalDuration(AudioObject *self) { 
    return ((_AudioObject*)self)->riffData.audioLength; 
}

AudioError * audioGetError(AudioObject *self) {
    return ((_AudioObject*)self)->error;
}

const char * audioGetErrorString(AudioError *error) {
    switch (error->type) {
        // info
        case AUDIO_ERROR_NO_ERROR:
            return "No error";
            
        // warnings
        case AUDIO_WARING_ALREADY_PLAYING:
            return "Audio is already playing";

        case AUDIO_WARNING_ALREADY_PAUSED:
            return "Audio is already paused";

        case AUDIO_WARNING_JUMPED_BEYOND_END:
            return "Jumped beyond end of audio";

        // errors
        // reading riff file
        case AUDIO_ERROR_FILE_TOO_SMALL:
            return "RIFF file is too small";

        case AUDIO_ERROR_INVALID_RIFF_MAGIC_NUMBER:
            return "RIFF magic is invalid";

        case AUDIO_ERROR_INVALID_WAVE_MAGIC_NUMBER:
            return "WAVE magic is invalid";

        case AUDIO_ERROR_INVALID_FILE_SIZE:
            return "RIFF file size is invalid";

        case AUDIO_ERROR_IMVALID_FMT_MAGIC_NUMBER:
            return "FMT magic is invalid";

        case AUDIO_ERROR_INVALID_FMT_SIZE:
            return "FMT size is invalid";

        case AUDIO_ERROR_NO_PCM_FORMAT:
            return "Audio format is not PCM";

        case AUDIO_ERROR_INVALID_BYTE_RATE:
            return "Byte rate is invalid";

        case AUDIO_ERROR_INVALID_BLOCK_ALIGN:
            return "Block align is invalid";

        case AUDIO_ERROR_DATA_CHUNK_NOT_FOUND:
            return "DATA chunk not found";

        case AUDIO_ERROR_INVALID_DATA_MAGIC_NUMBER:
            return "DATA magic is invalid";

        case AUDIO_ERROR_INVALID_DATA_SIZE:
            return "DATA size is invalid";

        // alsa
        case AUDIO_ERROR_ALSA_ERROR:
            return snd_strerror(error->alsaErrorNumber);

        // other
        case AUDIO_ERROR_MEMORY_ALLOCATION_FAILED:
            return "Memory allocation failed";

        case AUDIO_UNSUPPORTED_BITS_PER_SAMPLE:
            return "Unsupported bits per sample";

        default:
            return "Unknown error";
    }
}

void audioResetError(AudioObject *self) {
    ((_AudioObject*)self)->error->type = AUDIO_ERROR_NO_ERROR;
    ((_AudioObject*)self)->error->level = AUDIO_ERROR_LEVEL_INFO;
    ((_AudioObject*)self)->error->alsaErrorNumber = 0;
}
