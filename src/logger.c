#include "logger.h"
#include <stdio.h>
#include <time.h>

#define LOG_FILE "process_manager.log"

void log_action(const char *action, pid_t pid, const char *result) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return; /* không log được thì thôi, không làm crash chương trình chính */

    time_t now = time(NULL);
    char time_buf[32];
    struct tm *tm_info = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f, "[%s] ACTION=%s PID=%d RESULT=%s\n", time_buf, action, pid, result);
    fclose(f);
}
