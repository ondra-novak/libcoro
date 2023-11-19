#include <iostream>
#include <fstream>
#include <unordered_set>
#include <filesystem>


struct State {
    std::unordered_set<std::filesystem::path> processedFiles;
    std::unordered_set<std::string> unkownHeaders;
};

bool processFile(const std::filesystem::path& filename, State &state, std::ostream &out);


std::string_view trim(std::string_view x) {
    while (!x.empty() && std::isspace(x.back())) {
        x = x.substr(0, x.length()-1);
    }
    while (!x.empty() && std::isspace(x.front())) {
        x = x.substr(1);
    }
    return x;
}

void processLine(const std::filesystem::path &dir, const std::string& line, State &state, std::ostream &out) {
    std::string_view lnv(line);
    lnv = trim(lnv);
    if (lnv.empty()) {
        return;
    }else  if (lnv == "#pragma once") {
        // Pragma once, přeskočit
        return;
    } else if (lnv.compare(0,8,"#include") == 0) {
        // #include "soubor"
        auto b = lnv.find('"');
        auto e = lnv.rfind('"');
        if (b < e && e!=lnv.npos) {
            std::string file ( lnv.substr(b+1, e-b-1));
            auto pathname = std::filesystem::weakly_canonical(dir / file);

            if (std::filesystem::exists(pathname)) {

                if (state.processedFiles.find(pathname) == state.processedFiles.end()) {
                    // Soubor ještě nebyl zpracován
                    state.processedFiles.insert(pathname);
                    if (!processFile(pathname, state, out)) {
                        out << line << std::endl;
                    }
                }
                return;
            }
        }
        b = lnv.find('<');
        e = lnv.find('>');
        if (b < e  && e!=lnv.npos ) {
            std::string hdr ( lnv.substr(b+1, e-b-1));
            if (state.unkownHeaders.find(hdr) != state.unkownHeaders.end()) {
                return;
            }
            state.unkownHeaders.insert(hdr);
        }
        out << line << std::endl;
    } else {
        // Jiné řádky, zkopírovat do výstupu
        out << line << std::endl;
    }
}

bool processFile(const std::filesystem::path& filename, State &state, std::ostream &out) {
    std::ifstream inputFile(filename);
    if (!inputFile.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(inputFile, line)) {
        processLine(filename.parent_path(), line, state, out);
    }

    inputFile.close();
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Requies initial file" << std::endl;
        return 1;
    }
    std::ostream *out;
    std::ofstream fout;
    if (argc > 2) {
        fout.open(argv[2], std::ios::trunc);
        out = &fout;
    } else {
        out = &std::cout;
    }


    State state;
    processFile(argv[1], state, *out);

    return 0;
}
