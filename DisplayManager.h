#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdbool.h>
#include <stdarg.h>
#include "Channel.h"
#include "HardwareManager.h"

// Message levels for logging
typedef enum {
    MSG_INFO,
    MSG_WARN,
    MSG_ERROR,
    MSG_DEBUG
} MessageLevel;

// System status information
typedef struct {
    int active_boards;
    int total_boards;
    double loop_frequency_hz;
    double send_frequency_hz;
    int uptime_seconds;
    bool gps_connected;
    bool influxdb_connected;
} SystemStatus;

// Opaque DisplayManager structure
typedef struct DisplayManager DisplayManager;

// === Initialization and Cleanup ===
// Initialize the display manager with ncurses
DisplayManager* display_manager_init(void);

// Cleanup and restore terminal
void display_manager_cleanup(DisplayManager* dm);

// Check if ncurses is available on this system
bool display_manager_is_available(void);

// === Data Display Functions ===
// Update the measurements display area
void display_manager_update_measurements(DisplayManager* dm, const Channel* channels, int channel_count, const GPSData* gps);

// Update the system status bar
void display_manager_update_status(DisplayManager* dm, const SystemStatus* status);

// Add a message to the scrolling message area
void display_manager_add_message(DisplayManager* dm, MessageLevel level, const char* format, ...);

// Refresh the display (call this to make updates visible)
void display_manager_refresh(DisplayManager* dm);

// === Utility Functions ===
// Set the configuration file name for display
void display_manager_set_config_name(DisplayManager* dm, const char* config_name);

// Enable/disable debug message display
void display_manager_set_debug_enabled(DisplayManager* dm, bool enabled);

// Clear all messages from the message area
void display_manager_clear_messages(DisplayManager* dm);

// === Thread Safety ===
// Lock display for atomic updates (if using multiple threads)
void display_manager_lock(DisplayManager* dm);
void display_manager_unlock(DisplayManager* dm);

#endif // DISPLAY_MANAGER_H