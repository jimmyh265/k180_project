#include <cstdlib>
#include <iostream>

int main() {
    int ret = system("systemctl restart grand_yeah.service");
    if(ret == 0) {
        std::cout << "Service restarted successfully.\n";
    } else {
        std::cout << "Failed to restart service, code: " << ret << "\n";
    }
    return 0;
}
