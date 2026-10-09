#ifndef LOGGER_H
#define LOGGER_H

#include <sys/types.h>

/* Ghi 1 dòng log vào file process_manager.log, kèm timestamp.
 * Ví dụ: log_action("KILL", 1234, "Thanh cong."); */
void log_action(const char *action, pid_t pid, const char *result);

#endif
