Whisper runtime kept for this project:

- `whisper/whisper.cpp/build/bin/whisper-cli`
- `whisper/whisper.cpp/build/src/libwhisper.so*`
- `whisper/whisper.cpp/build/ggml/src/libggml*.so*`
- `whisper/whisper.cpp/models/ggml-base.en.bin`
- `whisper/whisper.cpp/LICENSE`

Everything else from the original GitHub repository was removed because the current
project only needs offline speech-to-text at runtime.

Current use in the project:
- `porcupine/src/picovoice.c` records 5 seconds of audio after the wake word
- then runs `whisper-cli`
- then prints the transcript
