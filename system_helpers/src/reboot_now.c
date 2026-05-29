#include <stdlib.h>
#include <unistd.h>

int main() {
    // 嘗試立即重開機
    int ret = system("systemctl reboot");
    return ret == -1 ? 1 : 0;
}
