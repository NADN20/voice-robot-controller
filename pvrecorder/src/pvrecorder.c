#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <dlfcn.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <signal.h>


#include "pv_recorder.h"


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


int main(void) {
    signal(SIGINT, interrupt_handler);


    const char *library_path = "/home/nadn20/picovoice/pvrecorder/lib/libpv_recorder.so";
        void *dl_handle = open_dl(library_path);
    if (!dl_handle) {
        fprintf(stderr, "failed to load dynamic library at `%s`.\n", library_path);
        exit(1);
    }


    const char *(*pv_recorder_status_to_string_func)(pv_recorder_status_t) = 
        load_symbol(dl_handle, "pv_recorder_status_to_string");
    if (!pv_recorder_status_to_string_func) {
        print_dl_error("failed to load `pv_recorder_status_to_string`");
        exit(1);
    }


    pv_recorder_status_t (*pv_recorder_init_func)(
        const int32_t, 
        const int32_t, 
        const int32_t,
        pv_recorder_t **) = load_symbol(dl_handle, "pv_recorder_init");
    if (!pv_recorder_init_func) {
        print_dl_error("failed to load `pv_recorder_init`");
        exit(1);
    }


    pv_recorder_status_t (*pv_recorder_start_func)(pv_recorder_t *) = load_symbol(dl_handle, "pv_recorder_start");
    if (!pv_recorder_start_func) {
        print_dl_error("failed to load `pv_recorder_start`");
        exit(1);
    }


    pv_recorder_status_t (*pv_recorder_read_func)(pv_recorder_t *, int16_t *) = 
        load_symbol(dl_handle, "pv_recorder_read");
    if (!pv_recorder_read_func) {
        print_dl_error("failed to load `pv_recorder_read`");
        exit(1);
    }


    pv_recorder_status_t (*pv_recorder_stop_func)(pv_recorder_t *) = load_symbol(dl_handle, "pv_recorder_stop");
    if (!pv_recorder_stop_func) {
        print_dl_error("failed to load `pv_recorder_stop`");
        exit(1);
    }


    void (*pv_recorder_delete_func)(pv_recorder_t *) = load_symbol(dl_handle, "pv_recorder_delete");
    if (!pv_recorder_delete_func) {
        print_dl_error("failed to load `pv_recorder_delete`");
        exit(1);
    }

    void (*pv_recorder_set_debug_logging_func)(pv_recorder_t *, bool) =
        load_symbol(dl_handle, "pv_recorder_set_debug_logging");
    if (!pv_recorder_set_debug_logging_func) {
        print_dl_error("failed to load `pv_recorder_set_debug_logging`");
        exit(1);
    }


    const int32_t frame_length = 512;
    const int32_t device_index = -1;
    const int32_t buffered_frame_count = 10;


    pv_recorder_t *recorder = NULL;
    pv_recorder_status_t status = pv_recorder_init_func(
            frame_length,
            device_index,
            buffered_frame_count,
            &recorder);
    if (status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to initialize device with %s.\n", pv_recorder_status_to_string_func(status));
        exit(1);
    }

    pv_recorder_set_debug_logging_func(recorder, true);

 
    status = pv_recorder_start_func(recorder);
    if (status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to start device with %s.\n", pv_recorder_status_to_string_func(status));
        exit(1);
    }


    int16_t *frame = malloc(frame_length * sizeof(int16_t));



    printf("Recording... Press Ctrl+C to stop.\n");
    while (!is_interrupted) {
        pv_recorder_status_t status = pv_recorder_read_func(recorder, frame);
        if (status != PV_RECORDER_STATUS_SUCCESS) {
            fprintf(stderr, "Failed to read audio frames with %s.\n", pv_recorder_status_to_string_func(status));
            exit(1);
        }


        printf("first sample = %d\n", frame[0]);
    }
    free(frame);


    status = pv_recorder_stop_func(recorder);
    if (status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to stop device with %s.\n", pv_recorder_status_to_string_func(status));
        exit(1);
    }


    printf("Stopped.\n");
    pv_recorder_delete_func(recorder);
    close_dl(dl_handle);
}
