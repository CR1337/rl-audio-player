[![GitHub license](https://img.shields.io/github/license/Naereen/StrapDown.js.svg)](https://github.com/CR1337/rl-audio-player/blob/main/LICENSE)

# rl-audio-player

A simple library that provides audio playback capabilities.

It can only play WAV files. It can play, pause and stop them. Also it can jump to different timestamps.

## Building

Install the dependencies in [apt-depenedencies.txt](https://github.com/CR1337/rl-audio-player/blob/main/apt-dependencies.txt) using
```bash
apt install $(cat apt-dependencies.txt)
```

Run the Makefile with 
```bash
make
```
Multiple files are created in `./build`. `./build/libaudio.so` is the shared library. `./build/main` is a small program to test the libraries capabilites.

## Testing
To run test programs you need to install the python requirements in [requirements.txt](https://github.com/CR1337/rl-audio-player/blob/main/requirements.txt) and the dependencies in [apt-test-depenedencies.txt](https://github.com/CR1337/rl-audio-player/blob/main/apt-test-dependencies.txt).

First create a virtual environment and activate it
```bash
python3 -m venv .venv
source .venv/bin/activate
```

Then install the dependencies
```bash
pip install -r requirements.txt
```

And the apt dependencies
```bash
apt install $(cat apt-test-dependencies.txt)
```

You can run the tests with
```bash
pytest
```

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

|Command|Effect                              |
|-------|------------------------------------|
|h      |Display help.                       |
|p      |Pause the playback.                 |
|r      |Resume/start the playback.          |
|s      |Stop the playback.                  |
|j T    |Jump to T milliseconds.,            |
|t      |Display the current milliseconds.   |
|v V    |Set the volume to V [0..100].       |
|?      |Display the current volume [0..100].|
|q      |Quit the program.                   |

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

// Set the global volume:
audioSetVolume(audio, 42);

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

// After executing one of these commands you might get a warning if you did something wrong. E.g. you might have jumped beyond the end of the audio data. The program is able to self recover from a warning. Everytime you call an audio* function (except for audioGetErrorString and audioGetError) the error gets resets.
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

### Windows Subsystem for Linux (WSL)

While the target system for this project is a Raspberry Pi, developers working on this project may be using Windows Subsystem for Linux (WSL) will potentially encounter an issue where audio playback does not work out of the box. Audio playback in WSL requires some additional configuration.

To enable audio in WSL:

1. Install the ALSA plugins:
   ```
   sudo apt install libasound2-plugins
   ```

2. Add the following lines to your `~/.asoundrc` file:
   ```
   pcm.!default { type pulse fallback "sysdefault" }
   ctl.!default { type pulse }
   ```

This configures ALSA to connect to a PulseAudio server as the default device. The PulseAudio WSL daemon acts as a bridge to transmit audio from the WSL environment to the Windows host.

After applying this configuration change, audio should work when running the rl-audio-player example program in WSL.

You can find more details and background information [here](https://github.com/CR1337/rl-audio-player/issues/2).
