#include "mini_os/alignment.h"

#include <cstddef>
#include <iostream>

int main() {
    std::size_t aligned = 0;

    if (!mini_os_align_up(4097, 4096, &aligned)) {
        std::cerr << "Alignment failed\n";

        return 1;
    }

    std::cout << "mini-os host demo: 4097 bytes aligned to a 4096-byte page = " << aligned << '\n';
}
