/* fakeapp: mo phong ung dung da tien trinh kieu Firefox/Chrome.
 * Tien trinh chinh tu chay lai CHINH file thuc thi cua no voi tham so "-contentproc N"
 * (giong cach Firefox tao Web Content...). Cay: root -> {con1 -> chau2, con3}. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void spawn(char *self, const char *id) {
    pid_t p = fork();
    if (p == 0) {
        execl(self, self, "-contentproc", id, (char *)NULL);
        _exit(127);
    }
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "-contentproc") == 0) {
        if (strcmp(argv[2], "1") == 0) spawn(argv[0], "2");   /* con 1 sinh them chau 2 */
        for (;;) sleep(60);
    }
    spawn(argv[0], "1");
    spawn(argv[0], "3");
    for (;;) sleep(60);
    return 0;
}
