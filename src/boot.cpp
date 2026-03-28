#include <iostream>
#include <fstream>
#include <array>

bool detectHeader(const std::string& filename) {
    std::cout << "SuperRecomp! Open!" << std::endl;

    std::ifstream RomFILE(filename, std::ios::binary);

    if (!RomFILE) {
        std::cerr << "Error! Could not find ROM file." << std::endl;
        return 1;
    } else {
        std::cout << "ROM file opened successfully." << std::endl;
    }

    std::array<char, 512> buffer;

    RomFILE.read(buffer.data(), buffer.size());

    if (buffer[0] == 'H' && buffer[1] == 'E' && buffer[2] == 'A' && buffer[3] == 'D') {
        return true;
    } else {
        return false;
    }
}

int main() {
    if (detectHeader("../smw.sfc")) {
        std::cout << "512-byte header detected!" << std::endl;
    } else {
        std::cout << "No 512-byte header found." << std::endl;
    }
    return 0;
}