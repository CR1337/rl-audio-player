[![GitHub license](https://img.shields.io/github/license/Naereen/StrapDown.js.svg)](https://github.com/CR1337/rl-audio-player/blob/main/LICENSE)

# rl-audio-player

A simple library that provides audio playback capailities.

It can only play WAV files. It can play, pause and stop them. Also it can jump to different timestamps.

## Building

Run the Makefile with 
```
make
```
Multiple files are created in `./build`. `./build/libaudio.so` is the shared library. `./build/main` is a small program to test the libraries capabilites.

## Usage

### Test program

You can play a WAV file using the test program by running
```
./build/main FILENAME
```
where `FILENAME` is the name of the WAV file to be played. You can also use
```
./build/main -m FILENAME
```
That way the file is not read into memory but mapped into virtual memory instead. This might be useful if you have few memory and a large file.
You can get some usage information with 
```
./build/main -h
```

Once the program is running you can control it by typing commands and pressing `Enter`.

|Command|Effect                           |
|-------|---------------------------------|
|p      |Pause the playback.              |
|r      |Resume/start the playback.       |
|s      |Stop the playback.               |
|j T    |Jump to T milliseconds.,         |
|t      |Display the current milliseconds.|
|q      |Quit the program.                |

### Library

```C
#include <stdio.h>
#include <stdint.h>
#include "audio.h"

/*...*/

// Get a pointer to the WAV file content either by reading it or by mapping it to memory.
void *rawData = /*...*/;

// Create an AudioConfiguration object and pass the rawData and its size. Also set the name of the playback device and its size. The timeResolution determines every how many milliseconds commands like audioPlay() or audioPause() are processed.
AudioConfiguration configuration = {
    .rawData = rawData,
    .rawDataSize = fileSize,
    .soundDeviceName = "default",
    .soundDeviceNameSize = 8,
    .timeResolution = 10
};

// Initialize a new AudioObject passing the configuration.
AudioObject audio = audioInit(&configuration);

// If audio == NULL the memory allocation for it failed.
if (audio == NULL) {
    exit(1);
}

// Even if audio != NULL there could be an error. You need to check for it
// and destroy the audio object.
AudioError *error = audioGetError(audio);
if (error->level == AUDIO_ERROR_LEVEL_ERROR) {
    const char *errorString = ausioGetErrorString(error);
    fprintf(stderr, "%s\n", errorString);
    audioDestroy(audio);
    exit(1);
}

// Now you can get the total length of the WAV file in milliseconds.
uint32_t totalDuration = audioGetTotalDuration(audio);
float totalDurationSeconds = totalDuration / 1000.0f;
printf("Total duration: %.2f seconds\n", totalDurationSeconds);

// You can play, pause, resume and stop the audio. Instead of NULL you can also pass a pthread_barrier_t in case you want to synchronize the audio thread with other threads.
audioPlay(audio, NULL);
/* Wait some time */
audioPause(audio, NULL);
/* Wait some time */
audioPlay(audio, NULL);  // resume
/* Wait some time */
audioStop(audio, NULL);

// After stopping you can play it again from the beginning.
audioPlay(audio, NULL);

// You can jump to a specific timestamp. Just specify the offset in milliseconds.
audioJump(audio, NULL, 4200);

// You can also get the current milliseconds.
uint32_t ms = audioGetCurrentTime(audio);
float currentTimeSeconds = currentTime / 1000.0f;
printf("Current time: %.2f seconds\n", currentTimeSeconds);

// After executing one of these commands you might get a warning if you did something wrong. E.g. you might have jumped beyond the end of the audio data. The program is able to self recover from a warning. Everytime you call an audio* function (except audioGetErrorString) the error gets resets.
audioJump(audio, NULL, 42000000);
error = audioGetError(audio);
if (error->level == AUDIO_ERROR_LEVEL_WARNING) {
    errorString = audioGetErrorString(error);
    fprintf(stderr, "%s\n", errorString);
}

// At the end just destroy the audio object.
audioDestroy(audio);

/*...*/

```
