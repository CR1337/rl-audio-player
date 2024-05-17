import pytest
import tempfile
import ctypes
import os
import time
import subprocess
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

@pytest.mark.parametrize("configuration", configurations)
def test_audio(configuration: Dict[str, int]):
    print("Testing audio with configuration:", flush=True)
    for key, value in configuration.items():
        print(f"{key}: {value}", flush=True)
    print(flush=True)

    file = tempfile.NamedTemporaryFile(suffix=".wav")

    command = [
        "sox", "-n", "-R",
        "-r", str(configuration["sample_rate"]),
        "-b", str(configuration["bit_depth"]),
        "-c", str(configuration["number_of_channels"]),
        file.name,
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

    with open(file.name, "rb") as file:
        buffer = bytearray(file.read())
    
    char_array = (ctypes.c_char * len(buffer)).from_buffer(buffer)
    raw_data_ptr = ctypes.cast(ctypes.pointer(char_array), ctypes.c_void_p)

    audioConfiguration = AudioConfiguration(
        rawData=raw_data_ptr,
        rawDataSize=os.path.getsize(file.name),
        soundDeviceName=str.encode("default"),
        soundDeviceNameSize=7,
        timeResolution=50  # ms
    )

    audio = ctypes.CDLL("build/libaudio.so")

    # define the function signatures
    audio.audioInit.argtypes = [ctypes.POINTER(AudioConfiguration)]
    audio.audioInit.restype = ctypes.POINTER(ctypes.c_void_p)
    audio.audioDestroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    audio.audioDestroy.restype = None

    audio.audioPlay.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p)]
    audio.audioPlay.restype = ctypes.c_bool
    audio.audioPause.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p)]
    audio.audioPause.restype = ctypes.c_bool
    audio.audioStop.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p)]
    audio.audioStop.restype = None
    audio.audioJump.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint64]
    audio.audioJump.restype = ctypes.c_bool

    audio.audioGetError.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    audio.audioGetError.restype = ctypes.POINTER(AudioError)
    audio.audioGetErrorString.argtypes = [ctypes.POINTER(AudioError)]
    audio.audioGetErrorString.restype = ctypes.c_char_p

    # initialize the audio object
    audio_object = audio.audioInit(ctypes.byref(audioConfiguration))
    assert audio_object is not None, "Failed to initialize audio object"
    assert (error := audio.audioGetError(audio_object)).contents.level == 0, audio.audioGetErrorString(error).decode("utf-8")

    # play the audio
    assert audio.audioPlay(audio_object, None), "Failed to play audio"
    assert (error := audio.audioGetError(audio_object)).contents.level == 0, audio.audioGetErrorString(error).decode("utf-8")

    time.sleep(configuration['duration'] / 4)

    # pause the audio
    assert audio.audioPause(audio_object, None), "Failed to pause audio"
    assert (error := audio.audioGetError(audio_object)).contents.level == 0, audio.audioGetErrorString(error).decode("utf-8")

    time.sleep(configuration['duration'] / 4)

    # jump
    assert audio.audioJump(audio_object, None, configuration['duration'] * 500), "Failed to jump audio"
    assert (error := audio.audioGetError(audio_object)).contents.level == 0, audio.audioGetErrorString(error).decode("utf-8")

    time.sleep(configuration['duration'] / 4)

    assert audio.audioPlay(audio_object, None), "Failed to play audio"
    assert (error := audio.audioGetError(audio_object)).contents.level == 0, audio.audioGetErrorString(error).decode("utf-8")

    time.sleep(configuration['duration'] / 4)

    # stop the audio
    audio.audioStop(audio_object, None)

    # destroy the audio object and free the memory
    audio.audioDestroy(audio_object)
