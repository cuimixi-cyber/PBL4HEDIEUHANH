#include "process_actions.h"
#include <signal.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdio.h>

ActionResult kill_process(pid_t pid, int sig) {
    /* Chặn thao tác nguy hiểm với tiến trình init/systemd */
    if (pid <= 1) return ACTION_ERR_INVALID_ARG;

    errno = 0;
    if (kill(pid, sig) == 0) {
        return ACTION_OK;
    }

    switch (errno) {
        case ESRCH:  return ACTION_ERR_NOT_FOUND;
        case EPERM:  return ACTION_ERR_PERMISSION;
        default:     return ACTION_ERR_UNKNOWN;
    }
}

ActionResult set_process_priority(pid_t pid, int nice_value) {
    if (pid <= 1) return ACTION_ERR_INVALID_ARG;
    if (nice_value < -20 || nice_value > 19) return ACTION_ERR_INVALID_ARG;

    errno = 0;
    if (setpriority(PRIO_PROCESS, (id_t)pid, nice_value) == 0 && errno == 0) {
        return ACTION_OK;
    }

    switch (errno) {
        case ESRCH:  return ACTION_ERR_NOT_FOUND;
        case EPERM:  return ACTION_ERR_PERMISSION;
        case EACCES: return ACTION_ERR_PERMISSION;
        default:     return ACTION_ERR_UNKNOWN;
    }
}

const char *action_result_to_string(ActionResult result) {
    switch (result) {
        case ACTION_OK:               return "Thanh cong.";
        case ACTION_ERR_NOT_FOUND:    return "Loi: khong tim thay tien trinh (PID khong ton tai).";
        case ACTION_ERR_PERMISSION:   return "Loi: khong du quyen (thu chay lai bang sudo).";
        case ACTION_ERR_INVALID_ARG:  return "Loi: tham so khong hop le.";
        default:                      return "Loi khong xac dinh.";
    }
}
