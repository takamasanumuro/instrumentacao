#define _POSIX_C_SOURCE 200809L //Enables POSIX functions such as sigaction to be exposed by C library headers
#include "ApplicationManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

// Global pointer to the app manager, allowing the signal handler to access it.
static ApplicationManager* g_app_manager = NULL;

/**
 * @brief Signal handler to catch SIGINT and SIGTERM for graceful shutdown.
 */
static void signal_handler(int signum) {
    if (!g_app_manager) return;
    app_manager_signal_shutdown(g_app_manager);
}

/**
 * @brief Prints a usage error message to stderr.
 */
static int usage_error(const char* prog_name) {
    fprintf(stderr, "Usage: %s <config-file.yaml>\n", prog_name);
    return 1;
}

/**
 * @brief The main entry point of the application.
 */
int main(int argc, char **argv) {
    // Check if correct number of arguments was provided
    if (argc != 2) {
        return usage_error(argv[0]);
    }

    const char* config_file = argv[1];

    // File accessibility validation before initialization
    if (access(config_file, R_OK) != 0) {
        perror("YAML config file not accessible");
        return 1;
    }

    // Set up signal handling for graceful shutdown
    // sigaction is used to due to increased portability and safety against race conditions
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask); // Don't block other signals in the handler
    sa.sa_flags = 0; // Or SA_RESTART to auto-restart syscalls

    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Failed to register signal handlers");
        return 1;
    }

    // Create and initialize the application manager with YAML config.
    g_app_manager = app_manager_create(config_file);
    if (!g_app_manager) {
        fprintf(stderr, "[Main] Application creation failed. Exiting.\n");
        return 1;
    }
    
    AppManagerError init_result = app_manager_init(g_app_manager);
    if (init_result != APP_SUCCESS) {
        fprintf(stderr, "[Main] Application initialization failed: %s\n", 
                app_manager_error_string(init_result));
        app_manager_destroy(g_app_manager);
        return 1;
    }

    // Run the main application loop.
    app_manager_run(g_app_manager);

    // Clean up and destroy the application manager.
    app_manager_destroy(g_app_manager);

    printf("[Main] Shutdown complete.\n");
    return 0;
}