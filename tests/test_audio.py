import ctypes
import os
import pytest
import subprocess
import tempfile
import time

from itertools import product
from typing import Dict, List


class AudioConfiguration(ctypes.Structure):
    _fields_ = [
        ("rawData", ctypes.c_void_p),
        ("rawDataSize", ctypes.c_size_t),
        ("soundDeviceName", ctypes.c_char_p),
        ("soundDeviceNameSize", ctypes.c_size_t),
        ("timeResolution", ctypes.c_uint32),
    ]


class AudioError(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("level", ctypes.c_int),
        ("alsaErrorNumber", ctypes.c_int)
    ]

sample_rates: List[int] = [8000, 44100]  # Hz
number_of_channels: List[int] = [1, 2, 3, 5]
bit_depths: List[int] = [8, 16, 24, 32]
durations: List[int] = [1]  # seconds

configurations: List[Dict[str, int]] = [
    {
        "sample_rate": sample_rate, 
        "number_of_channels": number_of_channels, 
        "bit_depth": bit_depth, 
        "duration": duration
    }
    for sample_rate, number_of_channels, bit_depth, duration 
    in product(sample_rates, number_of_channels, bit_depths, durations)
]


def synth_audio(filename: str, configuration: Dict[str, int]):
    command = [
        "sox", "-n", "-R",
        "-r", str(configuration["sample_rate"]),
        "-b", str(configuration["bit_depth"]),
        "-c", str(configuration["number_of_channels"]),
        filename,
        "synth",
        str(configuration["duration"]),
        "whitenoise"
    ]

    process = subprocess.Popen(
        command, 
        stdout=subprocess.PIPE, 
        stderr=subprocess.PIPE
    )
    process.wait()
    _, stderr = process.communicate()
    assert process.returncode == 0, f"Failed to generate audio: {stderr}"


def bind_libaudio() -> ctypes.CDLL:
    libaudio = ctypes.CDLL("build/libaudio.so")

    # define the function signatures:

    libaudio.audioInit.argtypes = [ctypes.POINTER(AudioConfiguration)]
    libaudio.audioInit.restype = ctypes.POINTER(ctypes.c_void_p)
    libaudio.audioDestroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    libaudio.audioDestroy.restype = None

    libaudio.audioPlay.argtypes = [
        ctypes.POINTER(ctypes.c_void_p), 
        ctypes.POINTER(ctypes.c_void_p)
    ]
    libaudio.audioPlay.restype = ctypes.c_bool
    libaudio.audioPause.argtypes = [
        ctypes.POINTER(ctypes.c_void_p), 
        ctypes.POINTER(ctypes.c_void_p)
    ]
    libaudio.audioPause.restype = ctypes.c_bool
    libaudio.audioStop.argtypes = [
        ctypes.POINTER(ctypes.c_void_p), 
        ctypes.POINTER(ctypes.c_void_p)
    ]
    libaudio.audioStop.restype = None
    libaudio.audioJump.argtypes = [
        ctypes.POINTER(ctypes.c_void_p), 
        ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint64
    ]
    libaudio.audioJump.restype = ctypes.c_bool

    libaudio.audioGetIsPlaying.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    libaudio.audioGetIsPlaying.restype = ctypes.c_bool
    libaudio.audioGetIsPaused.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    libaudio.audioGetIsPaused.restype = ctypes.c_bool
    libaudio.audioGetCurrentTime.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    libaudio.audioGetCurrentTime.restype = ctypes.c_uint64
    libaudio.audioGetTotalDuration.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    libaudio.audioGetTotalDuration.restype = ctypes.c_uint64

    libaudio.audioGetError.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    libaudio.audioGetError.restype = ctypes.POINTER(AudioError)
    libaudio.audioGetErrorString.argtypes = [ctypes.POINTER(AudioError)]
    libaudio.audioGetErrorString.restype = ctypes.c_char_p

    return libaudio


def create_audio_configuration(buffer: bytearray, file_size: int) -> AudioConfiguration:
    char_array = (ctypes.c_char * len(buffer)).from_buffer(buffer)
    raw_data_ptr = ctypes.cast(ctypes.pointer(char_array), ctypes.c_void_p)

    return AudioConfiguration(
        rawData=raw_data_ptr,
        rawDataSize=file_size,
        soundDeviceName=str.encode("default"),
        soundDeviceNameSize=7,
        timeResolution=50  # ms
    )


@pytest.mark.parametrize("configuration", configurations)
def test_audio(configuration: Dict[str, int]):
    print("Testing audio with configuration:", flush=True)
    for key, value in configuration.items():
        print(f"{key}: {value}", flush=True)
    print(flush=True)

    file = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    
    synth_audio(file.name, configuration)
    libaudio = bind_libaudio()

    with open(file.name, "rb") as file:
        buffer = bytearray(file.read())
        file_size = os.path.getsize(file.name)

    audio_configuration = create_audio_configuration(buffer, file_size)

    # initialize
    audio_object = libaudio.audioInit(ctypes.byref(audio_configuration))
    assert audio_object is not None, "Failed to initialize"
    assert (
        (error := libaudio.audioGetError(audio_object)).contents.level == 0, 
        libaudio.audioGetErrorString(error).decode("utf-8")
    )

    # get total duration
    assert (
        total_duration := libaudio.audioGetTotalDuration(audio_object)
    ) == configuration['duration'] * 1000, "Failed to get total duration"

    # get current time
    assert (
        current_time := libaudio.audioGetCurrentTime(audio_object)
    ) == 0, "Failed to get current time"

    # play
    assert libaudio.audioPlay(audio_object, None), "Failed to play"
    assert (
        (error := libaudio.audioGetError(audio_object)).contents.level == 0, 
        libaudio.audioGetErrorString(error).decode("utf-8")
    )

    # get is playing
    assert libaudio.audioGetIsPlaying(audio_object), "Failed to get is playing"

    time.sleep(configuration['duration'] / 4)

    # pause
    assert libaudio.audioPause(audio_object, None), "Failed to pause"
    assert (
        (error := libaudio.audioGetError(audio_object)).contents.level == 0, 
        libaudio.audioGetErrorString(error).decode("utf-8")
    )

    # get is paused
    assert libaudio.audioGetIsPaused(audio_object), "Failed to get is paused"

    time.sleep(configuration['duration'] / 4)

    # jump
    assert libaudio.audioJump(audio_object, None, 0), "Failed to jump"
    assert (
        (error := libaudio.audioGetError(audio_object)).contents.level == 0, 
        libaudio.audioGetErrorString(error).decode("utf-8")
    )

    time.sleep(configuration['duration'] / 4)

    # resume
    assert libaudio.audioPlay(audio_object, None), "Failed to resume"
    assert (
        (error := libaudio.audioGetError(audio_object)).contents.level == 0, 
        libaudio.audioGetErrorString(error).decode("utf-8")
    )

    time.sleep(configuration['duration'] / 4)

    # stop
    libaudio.audioStop(audio_object, None)

    # destroy
    libaudio.audioDestroy(audio_object)

    if os.path.exists(file.name):
        os.remove(file.name)
