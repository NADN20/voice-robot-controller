Project goal:
Build a voice-controlled robot interface that stays lightweight while idle.
The robot should not run speech-to-text all the time because that uses more CPU and memory.
Instead, it should wait for a wake word first, then listen to a short command, convert it to text, and later use that text to control the robot.

Main task:
The current task of this project is to implement this voice pipeline:

`A2rbot` wake word -> listen for 5 seconds -> convert speech to text -> return to sleep

Desired final behavior:
- The robot stays in sleep mode most of the time.
- Porcupine listens for the wake word.
- When the wake word is detected, the robot records a short command window.
- Whisper converts the recorded audio to text.
- That text can later be mapped to robot actions such as lock, unlock, move, stop, or other commands.

Why this design:
- Porcupine is lightweight and good for always-on wake word detection.
- Whisper is heavier and should only run after the robot is awakened.
- This reduces resource usage and makes the system more practical for a robot.

Project structure:
- `pvrecorder/`
  - Handles microphone input.
  - Reads raw audio frames from the mic.
  - Used as the audio input layer.
- `porcupine/`
  - Handles wake word detection.
  - Contains the main application logic in `porcupine/src/picovoice.c`.
  - Decides when the robot should wake up and start listening.
- `whisper/`
  - Handles speech-to-text.
  - The folder was reduced to a minimal runtime.
  - Used only after wake word detection to convert recorded speech into text.

Current code behavior:
- `porcupine/src/picovoice.c` dynamically loads PvRecorder and Porcupine shared libraries.
- It runs as a 3-state loop:
  - `ROBOT_STATE_SLEEP`: continuously reads microphone frames and checks the wake word.
  - `ROBOT_STATE_LISTEN`: after wake word detection, records exactly 5 seconds of audio.
  - `ROBOT_STATE_TRANSCRIBE`: saves audio to WAV, runs Whisper CLI, prints the transcript, then returns to sleep.
- Captured command audio is saved as `/tmp/a2rbot_command.wav`.

Current flow:
`wake word` -> `listen for 5 seconds` -> `Whisper STT` -> `print text` -> `sleep again`

Configuration:
- The code no longer uses personal absolute paths.
- It uses project-relative paths for Porcupine, PvRecorder, and Whisper runtime files.
- The Picovoice access key is not stored in the source code anymore.
- You must provide your access key through the `PICOVOICE_ACCESS_KEY` environment variable.

Porcupine setup:
- Create a Picovoice account and sign in to the Picovoice Console:
  - https://console.picovoice.ai/
- Get your `AccessKey` from the console.
- Create or train your custom wake word in the Picovoice Console and download the generated `.ppn` file.
- Official Porcupine platform page:
  - https://picovoice.ai/platform/porcupine/
- Porcupine quick start and documentation:
  - https://picovoice.ai/docs/quick-start/porcupine-python/

Custom wake word:
- After training your wake word, place the downloaded `.ppn` file inside:
  - `porcupine/resources/keyword_files/linux/`
- Then update `keyword_paths[]` in `porcupine/src/picovoice.c` to point to your wake word file.
- The access key must be exported before running:

```bash
export PICOVOICE_ACCESS_KEY="YOUR_ACCESS_KEY"
```

Whisper setup:
- This project uses the offline `whisper.cpp` runtime.
- The current code expects:
  - `whisper/whisper.cpp/build/bin/whisper-cli`
  - `whisper/whisper.cpp/models/ggml-base.en.bin`
- Official whisper.cpp repository:
  - https://github.com/ggml-org/whisper.cpp
- Official whisper.cpp documentation:
  - https://github.com/ggml-org/whisper.cpp/blob/master/README.md
- The Whisper model file is not stored in this GitHub repository because GitHub rejects files larger than 100 MB.
- Download the model separately and place it at:
  - `whisper/whisper.cpp/models/ggml-base.en.bin`
- If you need to rebuild or re-download models later, use the instructions from the whisper.cpp README.

Files currently important for the project:
- `porcupine/src/picovoice.c`
- `pvrecorder/src/pvrecorder.c`
- `pvrecorder/include/pv_recorder.h`
- `porcupine/include/pv_porcupine.h`
- `whisper/whisper.cpp/build/bin/whisper-cli`
- `whisper/whisper.cpp/models/ggml-base.en.bin`

Whisper runtime kept:
- `whisper/whisper.cpp/build/bin/whisper-cli`
- `whisper/whisper.cpp/build/src/libwhisper.so*`
- `whisper/whisper.cpp/build/ggml/src/libggml*.so*`
- `whisper/whisper.cpp/models/ggml-base.en.bin`
- `whisper/whisper.cpp/LICENSE`

Where to customize:
- Picovoice access key: `PICOVOICE_ACCESS_KEY` environment variable
- Wake word file: `keyword_paths[]` in `porcupine/src/picovoice.c`
- Wake sensitivity: `sensitivity[]` in `porcupine/src/picovoice.c`
- Command listening duration: `listen_seconds` in `porcupine/src/picovoice.c`
- Whisper binary path: `whisper_cli_path` in `porcupine/src/picovoice.c`
- Whisper model path: `whisper_model_path` in `porcupine/src/picovoice.c`

Build:
To build `picovoice`:

```bash
cd /home/nadn20/picovoice
gcc -Wall -Wextra -O2 -o picovoice porcupine/src/picovoice.c -ldl
```

To build `pvrecorder_tutorial`:

```bash
cd /home/nadn20/picovoice
gcc -Wall -Wextra -O2 -I/home/nadn20/picovoice/pvrecorder/include -o pvrecorder_tutorial pvrecorder/src/pvrecorder.c -ldl
```

Run:
Before running:

```bash
export PICOVOICE_ACCESS_KEY="YOUR_ACCESS_KEY"
export LD_LIBRARY_PATH=./pvrecorder/lib:$LD_LIBRARY_PATH
```

Run the main app:

```bash
cd /home/nadn20/picovoice
./picovoice
```

Run the recorder test:

```bash
cd /home/nadn20/picovoice
./pvrecorder_tutorial
```

Troubleshooting:
1. Enable debug logging in PvRecorder:

```c
pv_recorder_set_debug_logging(recorder, true);
```

2. Verify the selected input device:

```c
const char *selected_device = pv_recorder_get_selected_device(recorder);
fprintf(stdout, "Selected device: %s.\n", selected_device);
```

3. If you get ALSA or microphone errors at runtime, the code may still be correct and the issue may come from the local audio device configuration.

Next goal:
The next development step is to take the Whisper transcript and map it to real robot actions.
Examples:
- `"lock the robot"`
- `"unlock"`
- `"move forward"`
- `"stop"`
