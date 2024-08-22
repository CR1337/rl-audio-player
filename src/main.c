#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "audio.h"

void printCommands() {
    printf("h\t\tShow help.\n");
    printf("p\t\tPause playback.\n");
    printf("r\t\tResume/start playback.\n");
    printf("s\t\tStop playback.\n");
    printf("j T\t\tJump to T milliseconds.\n");
    printf("t\t\tShow current milliseconds.\n");
    printf("v V\t\tSet volume to V [0..100].\n");
    printf("?\t\tShow current volume [0..100].\n");
    printf("q\t\tQuit program.\n");
    putchar('\n');
}

void mainloop(AudioObject audio) {
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, 1);

    audioPlay(audio, &barrier);
    AudioError *error = audioGetError(audio);
    printf("Playing\nEnter 'q' to quit\n");

    while (true) {
        switch (getchar()) {
            case 'h':
                printCommands();
                break;

            case 'p':
                audioPause(audio, &barrier);
                printf("Paused\n");
                break;

            case 'r':
                audioPlay(audio, &barrier);
                printf("Play/Resumed\n");
                break;

            case 's':
                audioStop(audio, &barrier);
                printf("Stopped\n");
                break;

            case 'j':
                uint64_t milliseconds;
                if (scanf("%lu", &milliseconds) == EOF) {
                    fprintf(stderr, "Could not read time.");
                    break;
                }
                audioJump(audio, &barrier, milliseconds);
                printf("Jumped to %lu milliseconds\n", milliseconds);                
                break;

            case 't':
                uint64_t currentTime = audioGetCurrentTime(audio);
                float currentTimeSeconds = currentTime / 1000.0f;
                printf("Current time: %.2f seconds\n", currentTimeSeconds);
                break;

            case 'v':
                uint8_t volume;
                if (scanf("%hhu", &volume) == EOF) {
                    fprintf(stderr, "Could not read volume.");
                    break;
                }
                audioSetVolume(audio, volume);
                printf("Set volume to %u\n", volume);
                break;

            case '?':
                uint8_t currentVolume = audioGetVolume(audio);
                printf("Current volume: %u\n", currentVolume);
                break;

            case 'q':
                printf("Quitting\n");
                audioDestroy(audio);
                pthread_barrier_destroy(&barrier);
                return;

            default:
                printf("Unrecognized command. Type 'h' for help.\n");
                break;  
        }
        if (error->level == AUDIO_ERROR_LEVEL_WARNING) {
            const char *errorString = audioGetErrorString(error);
            fprintf(stderr, "Warning: %s\n", errorString);
        }
    }
}

void * readFile(FILE *file, size_t fileSize) {
    void *rawData = calloc(fileSize, sizeof(uint8_t));
    if (rawData == NULL) {
        perror("malloc");
        fclose(file);
        exit( EXIT_FAILURE);
    }

    if (fread(rawData, 1, fileSize, file) != fileSize) {
        perror("fread");
        free(rawData);
        fclose(file);
        exit(EXIT_FAILURE);
    }

    return rawData;
}

void * mapFile(int fileDescriptor,  size_t fileSize) {
    void *rawData = mmap(
        NULL, fileSize, PROT_READ, MAP_PRIVATE, fileDescriptor, 0
    );
    if (rawData == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    return rawData;
}

void printUsage(char *programName) {
    fprintf(stderr, "Usage: %s [-m | -h] <WAV file>\n", programName);
}

void printHelp(char *programName) {
    printUsage(programName);
    putchar('\n');

    printf("-m\t\tMap the file to memory instead of reading it.\n");
    printf("-h\t\tShow this help.\n");
    putchar('\n');

    printf("After program start type the following commands to control palyback:\n");
    putchar('\n');

    printCommands();
    printCommands();
}

void parseArguments(int argc, char *argv[], char **filename, bool *map, bool *showHelp) {
    switch (argc) {
        case 2:
            if (!strncmp(argv[1], "-h", strnlen(argv[1], 3))) {
                *showHelp = true;
                return;
            } else {
                *filename = argv[1];
            }
            break;

        case 3:
            if (!strncmp(argv[1], "-m", strnlen(argv[1], 3))) {
                *filename = argv[2];
                *map = true;
            } else if (!strncmp(argv[1], "-h", strnlen(argv[1], 3))) {
                *showHelp = true;
                return;
            } else {
                printUsage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;

        default:
            printUsage(argv[0]);
            exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    char *filename = NULL;
    bool map = false;
    bool showHelp = false;
    parseArguments(argc, argv, &filename, &map, &showHelp);

    if (showHelp) {
        printHelp(argv[0]);
        return EXIT_SUCCESS;
    }

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    int fileDescriptor = fileno(file);
    if (fileDescriptor == -1) {
        perror("fileno");
        exit(EXIT_FAILURE);
    }

    struct stat fileStats;
    if (fstat(fileDescriptor, &fileStats) == -1) {
        perror("fstat");
        fclose(file);
        return EXIT_FAILURE;
    }
    size_t fileSize = fileStats.st_size;

    void *rawData;
    if (map) {
        printf("Mapping file.\n");
        rawData = mapFile(fileDescriptor, fileSize);
    } else {
        printf("Reading file.\n");
        rawData = readFile(file, fileSize);
        fclose(file);
    }

    AudioConfiguration configuration = {
        .rawData = rawData,
        .rawDataSize = fileSize,
        .soundDeviceName = "default",
        .soundDeviceNameSize = 8,
        .timeResolution = 10
    };

    AudioObject audio = audioInit(&configuration);
    if (audio == NULL) {
        fprintf(stderr, "Failed to initialize audio\n");
        free(rawData);
        return EXIT_FAILURE;
    }
    AudioError *error = audioGetError(audio);
    if (error->level == AUDIO_ERROR_LEVEL_ERROR) {
        const char *errorString = audioGetErrorString(error);
        fprintf(stderr, "Error: %s\n", errorString);
        free(rawData);
        return EXIT_FAILURE;
    }

    uint32_t totalDuration = audioGetTotalDuration(audio);
    float totalDurationSeconds = totalDuration / 1000.0f;
    printf("Total duration: %.2f seconds\n", totalDurationSeconds);

    mainloop(audio);

    if (map) {
        if (munmap(rawData, fileSize) == -1) {
            perror("munmap");
            return EXIT_FAILURE;
        }
        if (fclose(file)) {
            perror("fclose");
            return EXIT_FAILURE;
        }
    } else {
        free(rawData);
    }
    return EXIT_SUCCESS;
}
