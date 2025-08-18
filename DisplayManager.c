#include "DisplayManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

// Try to include ncurses, but handle gracefully if not available
#ifdef HAVE_NCURSES
#include <ncurses.h>
#define NCURSES_AVAILABLE 1
#else
// Check for common ncurses installations
#if __has_include(<ncurses.h>)
#include <ncurses.h>
#define NCURSES_AVAILABLE 1
#elif __has_include(<ncurses/ncurses.h>)
#include <ncurses/ncurses.h>
#define NCURSES_AVAILABLE 1
#elif __has_include(<curses.h>)
#include <curses.h>
#define NCURSES_AVAILABLE 1
#else
#define NCURSES_AVAILABLE 0
// Define dummy macros for when ncurses is not available
#define WINDOW void
#define initscr() NULL
#define endwin()
#define refresh()
#define wrefresh(w)
#define newwin(h,w,y,x) NULL
#define delwin(w)
#define wprintw(w, ...) printf(__VA_ARGS__)
#define mvwprintw(w, y, x, ...) printf(__VA_ARGS__)
#define wattron(w, a)
#define wattroff(w, a)
#define werase(w)
#define box(w, v, h)
#define wmove(w, y, x)
#define wscrl(w)
#define COLOR_PAIR(n) 0
#define A_BOLD 0
#define A_REVERSE 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_WHITE 7
#define COLOR_BLACK 0
#endif
#endif

// Layout constants
#define MIN_TERMINAL_WIDTH 80
#define MIN_TERMINAL_HEIGHT 25
#define HEADER_HEIGHT 3
#define STATUS_HEIGHT 3
#define MESSAGE_AREA_HEIGHT 8
#define MEASUREMENT_AREA_MIN_HEIGHT 10

// Message buffer size
#define MAX_MESSAGES 100
#define MAX_MESSAGE_LENGTH 256

// Color pairs (only used if ncurses is available)
#define COLOR_PAIR_NORMAL 1
#define COLOR_PAIR_INFO 2
#define COLOR_PAIR_WARN 3
#define COLOR_PAIR_ERROR 4
#define COLOR_PAIR_HEADER 5
#define COLOR_PAIR_STATUS 6

// Message structure
typedef struct {
    time_t timestamp;
    MessageLevel level;
    char text[MAX_MESSAGE_LENGTH];
} Message;

// Display Manager structure
struct DisplayManager {
    // ncurses availability
    bool ncurses_available;
    bool initialized;
    
    // Windows
    WINDOW* header_win;
    WINDOW* measurement_win;
    WINDOW* status_win;
    WINDOW* message_win;
    
    // Screen dimensions
    int screen_height;
    int screen_width;
    int measurement_height;
    
    // Message buffer (circular buffer)
    Message messages[MAX_MESSAGES];
    int message_count;
    int message_start_idx;
    
    // Configuration
    char config_name[64];
    bool debug_enabled;
    time_t start_time;
    
    // Thread safety
    pthread_mutex_t mutex;
    
    // Fallback mode
    bool use_fallback;
    
    // stdout redirection for ncurses mode
    FILE* original_stdout;
};

// === Private Function Prototypes ===
static bool init_ncurses(DisplayManager* dm);
static void cleanup_ncurses(DisplayManager* dm);
static void create_windows(DisplayManager* dm);
static void destroy_windows(DisplayManager* dm);
static void draw_header(DisplayManager* dm);
static void draw_measurements(DisplayManager* dm, const Channel* channels, int channel_count, const GPSData* gps);
static void draw_status(DisplayManager* dm, const SystemStatus* status);
static void draw_messages(DisplayManager* dm);
static void add_message_internal(DisplayManager* dm, MessageLevel level, const char* text);
static const char* level_to_string(MessageLevel level);
static int level_to_color_pair(MessageLevel level);
static void fallback_print_measurements(const Channel* channels, int channel_count, const GPSData* gps);
static void fallback_print_message(MessageLevel level, const char* text);

// === Public API Implementation ===

bool display_manager_is_available(void) {
    return NCURSES_AVAILABLE;
}

DisplayManager* display_manager_init(void) {
    DisplayManager* dm = calloc(1, sizeof(DisplayManager));
    if (!dm) {
        fprintf(stderr, "DisplayManager: Failed to allocate memory\n");
        return NULL;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&dm->mutex, NULL) != 0) {
        fprintf(stderr, "DisplayManager: Failed to initialize mutex\n");
        free(dm);
        return NULL;
    }
    
    // Initialize basic fields
    dm->ncurses_available = NCURSES_AVAILABLE;
    dm->debug_enabled = false;
    dm->start_time = time(NULL);
    dm->message_count = 0;
    dm->message_start_idx = 0;
    dm->original_stdout = NULL;
    strcpy(dm->config_name, "unknown.yaml");
    
    // Try to initialize ncurses
    if (dm->ncurses_available && init_ncurses(dm)) {
        dm->initialized = true;
        dm->use_fallback = false;
        create_windows(dm);
        draw_header(dm);
        refresh();
    } else {
        // Fall back to standard output
        dm->use_fallback = true;
        dm->initialized = true;
        printf("DisplayManager: Using fallback mode (ncurses not available)\n");
    }
    
    return dm;
}

void display_manager_cleanup(DisplayManager* dm) {
    if (!dm) return;
    
    pthread_mutex_lock(&dm->mutex);
    
    if (!dm->use_fallback && dm->ncurses_available) {
        destroy_windows(dm);
        cleanup_ncurses(dm);
    }
    
    pthread_mutex_unlock(&dm->mutex);
    pthread_mutex_destroy(&dm->mutex);
    free(dm);
}

void display_manager_update_measurements(DisplayManager* dm, const Channel* channels, int channel_count, const GPSData* gps) {
    if (!dm || !dm->initialized) return;
    
    pthread_mutex_lock(&dm->mutex);
    
    if (dm->use_fallback) {
        fallback_print_measurements(channels, channel_count, gps);
    } else {
        draw_measurements(dm, channels, channel_count, gps);
    }
    
    pthread_mutex_unlock(&dm->mutex);
}

void display_manager_update_status(DisplayManager* dm, const SystemStatus* status) {
    if (!dm || !dm->initialized || !status) return;
    
    pthread_mutex_lock(&dm->mutex);
    
    if (!dm->use_fallback) {
        draw_status(dm, status);
    }
    // In fallback mode, status is printed as messages
    
    pthread_mutex_unlock(&dm->mutex);
}

void display_manager_add_message(DisplayManager* dm, MessageLevel level, const char* format, ...) {
    if (!dm || !dm->initialized || !format) return;
    
    // Skip debug messages if debug is disabled
    if (level == MSG_DEBUG && !dm->debug_enabled) return;
    
    char buffer[MAX_MESSAGE_LENGTH];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    pthread_mutex_lock(&dm->mutex);
    
    if (dm->use_fallback) {
        fallback_print_message(level, buffer);
    } else {
        add_message_internal(dm, level, buffer);
        draw_messages(dm);
    }
    
    pthread_mutex_unlock(&dm->mutex);
}

void display_manager_refresh(DisplayManager* dm) {
    if (!dm || !dm->initialized || dm->use_fallback) return;
    
    pthread_mutex_lock(&dm->mutex);
    
#if NCURSES_AVAILABLE
    if (dm->header_win) wrefresh(dm->header_win);
    if (dm->measurement_win) wrefresh(dm->measurement_win);
    if (dm->status_win) wrefresh(dm->status_win);
    if (dm->message_win) wrefresh(dm->message_win);
    refresh();
#endif
    
    pthread_mutex_unlock(&dm->mutex);
}

void display_manager_set_config_name(DisplayManager* dm, const char* config_name) {
    if (!dm || !config_name) return;
    
    pthread_mutex_lock(&dm->mutex);
    strncpy(dm->config_name, config_name, sizeof(dm->config_name) - 1);
    dm->config_name[sizeof(dm->config_name) - 1] = '\0';
    
    if (!dm->use_fallback) {
        draw_header(dm);
    }
    pthread_mutex_unlock(&dm->mutex);
}

void display_manager_set_debug_enabled(DisplayManager* dm, bool enabled) {
    if (!dm) return;
    
    pthread_mutex_lock(&dm->mutex);
    dm->debug_enabled = enabled;
    pthread_mutex_unlock(&dm->mutex);
}

void display_manager_clear_messages(DisplayManager* dm) {
    if (!dm || !dm->initialized) return;
    
    pthread_mutex_lock(&dm->mutex);
    dm->message_count = 0;
    dm->message_start_idx = 0;
    
    if (!dm->use_fallback) {
        draw_messages(dm);
    }
    pthread_mutex_unlock(&dm->mutex);
}

void display_manager_lock(DisplayManager* dm) {
    if (dm) pthread_mutex_lock(&dm->mutex);
}

void display_manager_unlock(DisplayManager* dm) {
    if (dm) pthread_mutex_unlock(&dm->mutex);
}

// === Private Function Implementations ===

static bool init_ncurses(DisplayManager* dm) {
#if NCURSES_AVAILABLE
    if (!initscr()) {
        return false;
    }
    
    // Get screen dimensions
    getmaxyx(stdscr, dm->screen_height, dm->screen_width);
    
    // Check minimum size
    if (dm->screen_width < MIN_TERMINAL_WIDTH || dm->screen_height < MIN_TERMINAL_HEIGHT) {
        endwin();
        fprintf(stderr, "DisplayManager: Terminal too small (need %dx%d, got %dx%d)\n",
                MIN_TERMINAL_WIDTH, MIN_TERMINAL_HEIGHT, dm->screen_width, dm->screen_height);
        return false;
    }
    
    // Configure ncurses
    cbreak();           // Disable line buffering
    noecho();           // Don't echo input
    curs_set(0);        // Hide cursor
    nodelay(stdscr, TRUE); // Non-blocking input
    
    // Save original stdout and redirect to suppress printf output during ncurses mode
    dm->original_stdout = stdout;
    stdout = fopen("/dev/null", "w");
    
    // Initialize colors if supported
    if (has_colors()) {
        start_color();
        init_pair(COLOR_PAIR_NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_INFO, COLOR_GREEN, COLOR_BLACK);
        init_pair(COLOR_PAIR_WARN, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_ERROR, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_PAIR_HEADER, COLOR_WHITE, COLOR_BLUE);
        init_pair(COLOR_PAIR_STATUS, COLOR_BLACK, COLOR_WHITE);
    }
    
    // Calculate layout
    dm->measurement_height = dm->screen_height - HEADER_HEIGHT - STATUS_HEIGHT - MESSAGE_AREA_HEIGHT;
    if (dm->measurement_height < MEASUREMENT_AREA_MIN_HEIGHT) {
        dm->measurement_height = MEASUREMENT_AREA_MIN_HEIGHT;
    }
    
    return true;
#else
    return false;
#endif
}

static void cleanup_ncurses(DisplayManager* dm) {
#if NCURSES_AVAILABLE
    // Restore original stdout
    if (dm->original_stdout) {
        fclose(stdout);
        stdout = dm->original_stdout;
        dm->original_stdout = NULL;
    }
    
    endwin();
#endif
}

static void create_windows(DisplayManager* dm) {
#if NCURSES_AVAILABLE
    int y_offset = 0;
    
    // Clear the screen first
    clear();
    refresh();
    
    // Header window
    dm->header_win = newwin(HEADER_HEIGHT, dm->screen_width, y_offset, 0);
    y_offset += HEADER_HEIGHT;
    
    // Measurement window - ensure it fits
    int available_height = dm->screen_height - HEADER_HEIGHT - STATUS_HEIGHT - MESSAGE_AREA_HEIGHT;
    if (available_height < MEASUREMENT_AREA_MIN_HEIGHT) {
        available_height = MEASUREMENT_AREA_MIN_HEIGHT;
    }
    dm->measurement_height = available_height;
    dm->measurement_win = newwin(dm->measurement_height, dm->screen_width, y_offset, 0);
    y_offset += dm->measurement_height;
    
    // Status window
    dm->status_win = newwin(STATUS_HEIGHT, dm->screen_width, y_offset, 0);
    y_offset += STATUS_HEIGHT;
    
    // Message window - adjust height to fit screen
    int message_height = dm->screen_height - y_offset;
    if (message_height < 3) message_height = 3; // Minimum height
    dm->message_win = newwin(message_height, dm->screen_width, y_offset, 0);
    
    // Enable scrolling for message window
    scrollok(dm->message_win, TRUE);
#endif
}

static void destroy_windows(DisplayManager* dm) {
#if NCURSES_AVAILABLE
    if (dm->header_win) { delwin(dm->header_win); dm->header_win = NULL; }
    if (dm->measurement_win) { delwin(dm->measurement_win); dm->measurement_win = NULL; }
    if (dm->status_win) { delwin(dm->status_win); dm->status_win = NULL; }
    if (dm->message_win) { delwin(dm->message_win); dm->message_win = NULL; }
#endif
}

static void draw_header(DisplayManager* dm) {
#if NCURSES_AVAILABLE
    if (!dm->header_win) return;
    
    werase(dm->header_win);
    box(dm->header_win, 0, 0);
    
    wattron(dm->header_win, A_BOLD);
    mvwprintw(dm->header_win, 1, 2, "Instrumentation Monitor - %s", dm->config_name);
    mvwprintw(dm->header_win, 1, dm->screen_width - 12, "[Connected]");
    wattroff(dm->header_win, A_BOLD);
    
    wrefresh(dm->header_win);
#endif
}

static void draw_measurements(DisplayManager* dm, const Channel* channels, int channel_count, const GPSData* gps) {
#if NCURSES_AVAILABLE
    if (!dm->measurement_win) return;
    
    werase(dm->measurement_win);
    box(dm->measurement_win, 0, 0);
    
    wattron(dm->measurement_win, A_BOLD);
    mvwprintw(dm->measurement_win, 1, 2, "=== MEASUREMENTS ===");
    wattroff(dm->measurement_win, A_BOLD);
    
    int line = 2;
    int max_line = dm->measurement_height - 4; // Leave space for GPS and border
    
    // Display channel measurements
    for (int i = 0; i < channel_count && i < MAX_TOTAL_CHANNELS && line < max_line; i++) {
        if (channels[i].is_active) {
            double calibrated_value = channel_get_calibrated_value(&channels[i]);
            
            // Truncate long channel names to fit window width
            char display_id[50];
            strncpy(display_id, channels[i].id, sizeof(display_id) - 1);
            display_id[sizeof(display_id) - 1] = '\0';
            
            // Format line to fit window width
            char line_buffer[256];
            snprintf(line_buffer, sizeof(line_buffer),
                    "[Board 0x%02X] Ch%d (%.50s): %.2f %s",
                    channels[i].board_address,
                    channels[i].pin,
                    display_id,
                    calibrated_value,
                    channels[i].unit);
            
            // Truncate if too long for window
            if (strlen(line_buffer) > dm->screen_width - 4) {
                line_buffer[dm->screen_width - 7] = '.';
                line_buffer[dm->screen_width - 6] = '.';
                line_buffer[dm->screen_width - 5] = '.';
                line_buffer[dm->screen_width - 4] = '\0';
            }
            
            mvwprintw(dm->measurement_win, line++, 2, "%s", line_buffer);
        }
    }
    
    // Display GPS data if there's space
    if (line < max_line + 1) {
        line++; // Add spacing
        wattron(dm->measurement_win, A_BOLD);
        mvwprintw(dm->measurement_win, line++, 2, "=== GPS DATA ===");
        wattroff(dm->measurement_win, A_BOLD);
        
        if (gps && !isnan(gps->latitude) && !isnan(gps->longitude)) {
            mvwprintw(dm->measurement_win, line++, 2, 
                     "Lat: %.6f, Lon: %.6f, Speed: %.1f kph",
                     gps->latitude, gps->longitude, gps->speed);
        } else {
            wattron(dm->measurement_win, COLOR_PAIR(COLOR_PAIR_WARN));
            mvwprintw(dm->measurement_win, line++, 2, "GPS: No valid data");
            wattroff(dm->measurement_win, COLOR_PAIR(COLOR_PAIR_WARN));
        }
    }
    
    wrefresh(dm->measurement_win);
#endif
}

static void draw_status(DisplayManager* dm, const SystemStatus* status) {
#if NCURSES_AVAILABLE
    if (!dm->status_win || !status) return;
    
    werase(dm->status_win);
    box(dm->status_win, 0, 0);
    
    wattron(dm->status_win, A_BOLD);
    mvwprintw(dm->status_win, 1, 2, "=== SYSTEM STATUS ===");
    wattroff(dm->status_win, A_BOLD);
    
    int uptime_minutes = status->uptime_seconds / 60;
    mvwprintw(dm->status_win, 2, 2, 
             "I2C Boards: %d/%d active | Loop: %.1fHz | Send: %.1fHz | Uptime: %dm",
             status->active_boards, status->total_boards,
             status->loop_frequency_hz, status->send_frequency_hz,
             uptime_minutes);
    
    wrefresh(dm->status_win);
#endif
}

static void draw_messages(DisplayManager* dm) {
#if NCURSES_AVAILABLE
    if (!dm->message_win) return;
    
    werase(dm->message_win);
    box(dm->message_win, 0, 0);
    
    wattron(dm->message_win, A_BOLD);
    mvwprintw(dm->message_win, 1, 2, "=== MESSAGES ===");
    wattroff(dm->message_win, A_BOLD);
    
    // Get actual window dimensions
    int win_height, win_width;
    getmaxyx(dm->message_win, win_height, win_width);
    int display_lines = win_height - 3;  // Account for border and header
    
    if (display_lines <= 0) return;
    
    int start_msg = 0;
    if (dm->message_count > display_lines) {
        start_msg = dm->message_count - display_lines;
    }
    
    for (int i = 0; i < display_lines && i < dm->message_count; i++) {
        int msg_idx = (dm->message_start_idx + start_msg + i) % MAX_MESSAGES;
        const Message* msg = &dm->messages[msg_idx];
        
        struct tm* tm_info = localtime(&msg->timestamp);
        char time_str[9];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        // Format message to fit window width
        char msg_buffer[512];
        snprintf(msg_buffer, sizeof(msg_buffer), "[%s] %s: %.100s",
                time_str, level_to_string(msg->level), msg->text);
        
        // Truncate if too long for window
        if (strlen(msg_buffer) > win_width - 4) {
            msg_buffer[win_width - 7] = '.';
            msg_buffer[win_width - 6] = '.';
            msg_buffer[win_width - 5] = '.';
            msg_buffer[win_width - 4] = '\0';
        }
        
        int color = level_to_color_pair(msg->level);
        if (color > 0) wattron(dm->message_win, COLOR_PAIR(color));
        
        mvwprintw(dm->message_win, 2 + i, 2, "%s", msg_buffer);
        
        if (color > 0) wattroff(dm->message_win, COLOR_PAIR(color));
    }
    
    wrefresh(dm->message_win);
#endif
}

static void add_message_internal(DisplayManager* dm, MessageLevel level, const char* text) {
    if (dm->message_count < MAX_MESSAGES) {
        int idx = dm->message_count;
        dm->messages[idx].timestamp = time(NULL);
        dm->messages[idx].level = level;
        strncpy(dm->messages[idx].text, text, MAX_MESSAGE_LENGTH - 1);
        dm->messages[idx].text[MAX_MESSAGE_LENGTH - 1] = '\0';
        dm->message_count++;
    } else {
        // Circular buffer: overwrite oldest message
        int idx = dm->message_start_idx;
        dm->messages[idx].timestamp = time(NULL);
        dm->messages[idx].level = level;
        strncpy(dm->messages[idx].text, text, MAX_MESSAGE_LENGTH - 1);
        dm->messages[idx].text[MAX_MESSAGE_LENGTH - 1] = '\0';
        dm->message_start_idx = (dm->message_start_idx + 1) % MAX_MESSAGES;
    }
}

static const char* level_to_string(MessageLevel level) {
    switch (level) {
        case MSG_INFO: return "INFO";
        case MSG_WARN: return "WARN";
        case MSG_ERROR: return "ERROR";
        case MSG_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

static int level_to_color_pair(MessageLevel level) {
    switch (level) {
        case MSG_INFO: return COLOR_PAIR_INFO;
        case MSG_WARN: return COLOR_PAIR_WARN;
        case MSG_ERROR: return COLOR_PAIR_ERROR;
        case MSG_DEBUG: return COLOR_PAIR_NORMAL;
        default: return COLOR_PAIR_NORMAL;
    }
}

static void fallback_print_measurements(const Channel* channels, int channel_count, const GPSData* gps) {
    printf("--- Measurements ---\n");
    for (int i = 0; i < channel_count && i < MAX_TOTAL_CHANNELS; i++) {
        if (channels[i].is_active) {
            printf("[Board 0x%02X] Channel %d (%s): %.2f %s\n",
                   channels[i].board_address,
                   channels[i].pin,
                   channels[i].id,
                   channel_get_calibrated_value(&channels[i]),
                   channels[i].unit);
        }
    }
    
    if (gps && !isnan(gps->latitude) && !isnan(gps->longitude)) {
        printf("GPS: Lat=%.6f, Lon=%.6f, Speed=%.1f kph\n",
               gps->latitude, gps->longitude, gps->speed);
    } else {
        printf("GPS: No valid data\n");
    }
    printf("-------------------------\n");
}

static void fallback_print_message(MessageLevel level, const char* text) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[9];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    printf("[%s] %s: %s\n", time_str, level_to_string(level), text);
}