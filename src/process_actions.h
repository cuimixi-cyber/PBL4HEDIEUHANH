#ifndef PROCESS_ACTIONS_H
#define PROCESS_ACTIONS_H

#include <sys/types.h>

typedef enum {
    ACTION_OK = 0,
    ACTION_ERR_NOT_FOUND,     /* ESRCH - PID không tồn tại */
    ACTION_ERR_PERMISSION,    /* EPERM - không đủ quyền */
    ACTION_ERR_INVALID_ARG,   /* tham số không hợp lệ (VD: nice ngoài khoảng) */
    ACTION_ERR_UNKNOWN
} ActionResult;

/* Gửi tín hiệu 'sig' (VD SIGTERM, SIGKILL) đến tiến trình pid */
ActionResult kill_process(pid_t pid, int sig);

/* Đổi độ ưu tiên (nice value, khoảng -20..19) của tiến trình pid */
ActionResult set_process_priority(pid_t pid, int nice_value);

/* Chuyển ActionResult thành thông báo tiếng Việt để hiển thị cho người dùng */
const char *action_result_to_string(ActionResult result);

#endif
