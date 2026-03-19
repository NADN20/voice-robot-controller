#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/pv_porcupine.h"
#include "../../pvrecorder/include/pv_recorder.h"

static volatile bool is_interrupted = false;

void interrupt_handler(int _) {
    (void) _;
    is_interrupted = true;
}

static void *open_dl(const char *dl_path) {
#if defined(_WIN32) || defined(_WIN64)
    return LoadLibrary(dl_path);
#else
    return dlopen(dl_path, RTLD_NOW);
#endif
}

static void *load_symbol(void *handle, const char *symbol) {
#if defined(_WIN32) || defined(_WIN64)
    return GetProcAddress((HMODULE) handle, symbol);
#else
    return dlsym(handle, symbol);
#endif
}

static void close_dl(void *handle) {
#if defined(_WIN32) || defined(_WIN64)
    FreeLibrary((HMODULE) handle);
#else
    dlclose(handle);
#endif
}

static void print_dl_error(const char *message) {
#if defined(_WIN32) || defined(_WIN64)
    fprintf(stderr, "%s with code '%lu'.\n", message, GetLastError());
#else
    fprintf(stderr, "%s with `%s`.\n", message, dlerror());
#endif
}

static bool write_wav_mono_16bit(
        const char *path,
        const int16_t *samples,
        int32_t num_samples,
        int32_t sample_rate) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open WAV file `%s`: %s\n", path, strerror(errno));
        return false;
    }

    const int16_t num_channels = 1;
    const int16_t bits_per_sample = 16;
    const int32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    const int16_t block_align = num_channels * (bits_per_sample / 8);
    const int32_t data_bytes = num_samples * (int32_t) sizeof(int16_t);
    const int32_t riff_chunk_size = 36 + data_bytes;

    if (fwrite("RIFF", 1, 4, fp) != 4 ||
        fwrite(&riff_chunk_size, sizeof(riff_chunk_size), 1, fp) != 1 ||
        fwrite("WAVE", 1, 4, fp) != 4 ||
        fwrite("fmt ", 1, 4, fp) != 4) {
        fclose(fp);
        return false;
    }

    const int32_t fmt_chunk_size = 16;
    const int16_t audio_format_pcm = 1;
    if (fwrite(&fmt_chunk_size, sizeof(fmt_chunk_size), 1, fp) != 1 ||
        fwrite(&audio_format_pcm, sizeof(audio_format_pcm), 1, fp) != 1 ||
        fwrite(&num_channels, sizeof(num_channels), 1, fp) != 1 ||
        fwrite(&sample_rate, sizeof(sample_rate), 1, fp) != 1 ||
        fwrite(&byte_rate, sizeof(byte_rate), 1, fp) != 1 ||
        fwrite(&block_align, sizeof(block_align), 1, fp) != 1 ||
        fwrite(&bits_per_sample, sizeof(bits_per_sample), 1, fp) != 1 ||
        fwrite("data", 1, 4, fp) != 4 ||
        fwrite(&data_bytes, sizeof(data_bytes), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    if (fwrite(samples, sizeof(int16_t), (size_t) num_samples, fp) != (size_t) num_samples) {
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}

static bool run_whisper(
        const char *whisper_cli_path,
        const char *model_path,
        const char *wav_path,
        char *out_text,
        size_t out_text_size) {
    char command[2048];
    snprintf(
            command,
            sizeof(command),
            "%s -m %s -l en -nt -np -f %s 2>/dev/null",
            whisper_cli_path,
            model_path,
            wav_path);

    FILE *pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Failed to run whisper command.\n");
        return false;
    }

    out_text[0] = '\0';
    char line[1024];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        char *start = line;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
            start++;
        }
        if (*start == '\0') {
            continue;
        }

        char *end = start + strlen(start);
        while (end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        *end = '\0';
        snprintf(out_text, out_text_size, "%s", start);
    }

    const int exit_code = pclose(pipe);
    if (exit_code != 0) {
        fprintf(stderr, "Whisper exited with code %d.\n", exit_code);
        return false;
    }

    return out_text[0] != '\0';
}

typedef enum {
    ROBOT_STATE_SLEEP = 0,
    ROBOT_STATE_LISTEN,
    ROBOT_STATE_TRANSCRIBE
} robot_state_t;

int main(void) {
    signal(SIGINT, interrupt_handler);

    const char *pv_recorder_library_path = "pvrecorder/lib/libpv_recorder.so";
    void *recorder_library = open_dl(pv_recorder_library_path);
    if (!recorder_library) {
        fprintf(stderr, "failed to load dynamic library at `%s`.\n", pv_recorder_library_path);
        return 1;
    }

    const char *(*pv_recorder_status_to_string_func)(pv_recorder_status_t) =
        load_symbol(recorder_library, "pv_recorder_status_to_string");
    if (!pv_recorder_status_to_string_func) {
        print_dl_error("failed to load `pv_recorder_status_to_string`");
        return 1;
    }

    pv_recorder_status_t (*pv_recorder_init_func)(const int32_t, const int32_t, const int32_t, pv_recorder_t **) =
        load_symbol(recorder_library, "pv_recorder_init");
    if (!pv_recorder_init_func) {
        print_dl_error("failed to load `pv_recorder_init`");
        return 1;
    }

    pv_recorder_status_t (*pv_recorder_start_func)(pv_recorder_t *) =
        load_symbol(recorder_library, "pv_recorder_start");
    if (!pv_recorder_start_func) {
        print_dl_error("failed to load `pv_recorder_start`");
        return 1;
    }

    pv_recorder_status_t (*pv_recorder_read_func)(pv_recorder_t *, int16_t *) =
        load_symbol(recorder_library, "pv_recorder_read");
    if (!pv_recorder_read_func) {
        print_dl_error("failed to load `pv_recorder_read`");
        return 1;
    }

    pv_recorder_status_t (*pv_recorder_stop_func)(pv_recorder_t *) =
        load_symbol(recorder_library, "pv_recorder_stop");
    if (!pv_recorder_stop_func) {
        print_dl_error("failed to load `pv_recorder_stop`");
        return 1;
    }

    void (*pv_recorder_delete_func)(pv_recorder_t *) = load_symbol(recorder_library, "pv_recorder_delete");
    if (!pv_recorder_delete_func) {
        print_dl_error("failed to load `pv_recorder_delete`");
        return 1;
    }

    const char *porcupine_library_path = "porcupine/lib/libpv_porcupine.so";
    void *porcupine_library = open_dl(porcupine_library_path);
    if (!porcupine_library) {
        fprintf(stderr, "failed to load dynamic library at `%s`.\n", porcupine_library_path);
        return 1;
    }

    const char *(*pv_status_to_string_func)(pv_status_t) = load_symbol(porcupine_library, "pv_status_to_string");
    if (!pv_status_to_string_func) {
        print_dl_error("failed to load `pv_status_to_string`");
        return 1;
    }

    pv_status_t (*pv_porcupine_init_func)(
            const char *,
            const char *,
            const char *,
            int32_t,
            const char *const *,
            const float *,
            pv_porcupine_t **) = load_symbol(porcupine_library, "pv_porcupine_init");
    if (!pv_porcupine_init_func) {
        print_dl_error("failed to load `pv_porcupine_init`");
        return 1;
    }

    void (*pv_porcupine_delete_func)(pv_porcupine_t *) = load_symbol(porcupine_library, "pv_porcupine_delete");
    if (!pv_porcupine_delete_func) {
        print_dl_error("failed to load `pv_porcupine_delete`");
        return 1;
    }

    pv_status_t (*pv_porcupine_process_func)(pv_porcupine_t *, const int16_t *, int32_t *) =
        load_symbol(porcupine_library, "pv_porcupine_process");
    if (!pv_porcupine_process_func) {
        print_dl_error("failed to load `pv_porcupine_process`");
        return 1;
    }

    int32_t (*pv_porcupine_frame_length_func)() = load_symbol(porcupine_library, "pv_porcupine_frame_length");
    if (!pv_porcupine_frame_length_func) {
        print_dl_error("failed to load `pv_porcupine_frame_length`");
        return 1;
    }

    int32_t (*pv_sample_rate_func)() = load_symbol(porcupine_library, "pv_sample_rate");
    if (!pv_sample_rate_func) {
        print_dl_error("failed to load `pv_sample_rate`");
        return 1;
    }

    const char *access_key = getenv("PICOVOICE_ACCESS_KEY");
    const char *porcupine_model_path = "porcupine/model/porcupine_params.pv";
    char *device = "best";
    const int32_t num_keywords = 1;
    const char *keyword_paths[] = {
        "porcupine/resources/keyword_files/linux/A-to-R-ROBOT_en_linux_v4_0_0.ppn"
    };
    float sensitivity[] = {0.5f};

    const char *whisper_cli_path = "whisper/whisper.cpp/build/bin/whisper-cli";
    const char *whisper_model_path = "whisper/whisper.cpp/models/ggml-base.en.bin";
    const char *command_wav_path = "/tmp/a2rbot_command.wav";

    if (!access_key || access_key[0] == '\0') {
        fprintf(stderr, "Missing PICOVOICE_ACCESS_KEY environment variable.\n");
        fprintf(stderr, "Set it before running, for example:\n");
        fprintf(stderr, "export PICOVOICE_ACCESS_KEY=\"YOUR_ACCESS_KEY\"\n");
        return 1;
    }

    pv_porcupine_t *porcupine = NULL;
    pv_status_t status = pv_porcupine_init_func(
            access_key,
            porcupine_model_path,
            device,
            num_keywords,
            keyword_paths,
            sensitivity,
            &porcupine);
    if (status != PV_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to init Porcupine with `%s`\n", pv_status_to_string_func(status));
        return 1;
    }

    const int32_t frame_length = pv_porcupine_frame_length_func();
    const int32_t sample_rate = pv_sample_rate_func();

    pv_recorder_t *recorder = NULL;
    pv_recorder_status_t recorder_status = pv_recorder_init_func(
            frame_length,
            -1,
            100,
            &recorder);
    if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to initialize device with %s.\n", pv_recorder_status_to_string_func(recorder_status));
        pv_porcupine_delete_func(porcupine);
        return 1;
    }

    recorder_status = pv_recorder_start_func(recorder);
    if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to start device with %s.\n", pv_recorder_status_to_string_func(recorder_status));
        pv_recorder_delete_func(recorder);
        pv_porcupine_delete_func(porcupine);
        return 1;
    }

    int16_t *frame = malloc((size_t) frame_length * sizeof(int16_t));
    if (!frame) {
        fprintf(stderr, "Failed to allocate frame buffer.\n");
        pv_recorder_stop_func(recorder);
        pv_recorder_delete_func(recorder);
        pv_porcupine_delete_func(porcupine);
        return 1;
    }

    const int listen_seconds = 5;
    const int32_t frames_per_command = (sample_rate * listen_seconds) / frame_length;
    const int32_t command_num_samples = frames_per_command * frame_length;
    int16_t *command_samples = malloc((size_t) command_num_samples * sizeof(int16_t));
    if (!command_samples) {
        fprintf(stderr, "Failed to allocate command buffer.\n");
        free(frame);
        pv_recorder_stop_func(recorder);
        pv_recorder_delete_func(recorder);
        pv_porcupine_delete_func(porcupine);
        return 1;
    }

    robot_state_t state = ROBOT_STATE_SLEEP;
    printf("A2rbot ready. Say wake word to capture command for %d seconds. Press Ctrl+C to stop.\n", listen_seconds);

    while (!is_interrupted) {
        if (state == ROBOT_STATE_SLEEP) {
            recorder_status = pv_recorder_read_func(recorder, frame);
            if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
                fprintf(stderr, "Failed to read audio frames with %s.\n", pv_recorder_status_to_string_func(recorder_status));
                break;
            }

            int32_t keyword_index = -1;
            status = pv_porcupine_process_func(porcupine, frame, &keyword_index);
            if (status != PV_STATUS_SUCCESS) {
                fprintf(stderr, "'pv_porcupine_process' failed with '%s'\n", pv_status_to_string_func(status));
                break;
            }

            if (keyword_index != -1) {
                printf("[WAKE] Wake word detected. Listening now...\n");
                fflush(stdout);
                state = ROBOT_STATE_LISTEN;
            }
        } else if (state == ROBOT_STATE_LISTEN) {
            bool capture_error = false;
            for (int32_t i = 0; i < frames_per_command && !is_interrupted; i++) {
                recorder_status = pv_recorder_read_func(recorder, frame);
                if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
                    fprintf(stderr, "Failed to read command audio with %s.\n", pv_recorder_status_to_string_func(recorder_status));
                    capture_error = true;
                    break;
                }

                memcpy(
                        command_samples + ((size_t) i * (size_t) frame_length),
                        frame,
                        (size_t) frame_length * sizeof(int16_t));
            }

            if (capture_error || is_interrupted) {
                break;
            }

            if (!write_wav_mono_16bit(command_wav_path, command_samples, command_num_samples, sample_rate)) {
                fprintf(stderr, "Failed to write command WAV.\n");
                break;
            }

            printf("[LISTEN] Captured %d seconds of audio.\n", listen_seconds);
            fflush(stdout);
            state = ROBOT_STATE_TRANSCRIBE;
        } else if (state == ROBOT_STATE_TRANSCRIBE) {
            char transcript[2048];
            bool whisper_ok = run_whisper(
                    whisper_cli_path,
                    whisper_model_path,
                    command_wav_path,
                    transcript,
                    sizeof(transcript));

            if (!whisper_ok) {
                fprintf(stderr, "[STT] Whisper failed or returned empty transcript.\n");
            } else {
                printf("[STT] %s\n", transcript);
                printf("[EXECUTE] TODO: map transcript to robot actions (lock/unlock/move/etc).\n");
            }

#if defined(_WIN32) || defined(_WIN64)
            Sleep(1500);
#else
            usleep(1500 * 1000);
#endif
            printf("[SLEEP] Waiting for wake word...\n");
            fflush(stdout);
            state = ROBOT_STATE_SLEEP;
        }
    }

    printf("Stopped.\n");
    free(command_samples);
    free(frame);
    pv_recorder_stop_func(recorder);
    pv_recorder_delete_func(recorder);
    pv_porcupine_delete_func(porcupine);
    close_dl(recorder_library);
    close_dl(porcupine_library);

    return 0;
}
