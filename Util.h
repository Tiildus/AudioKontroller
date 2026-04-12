#pragma once
#include <fstream>
#include <string>

// Reads /proc/<pid>/comm to get the process's binary name (e.g. "discord",
// "firefox"). Returns an empty string if the file can't be read (process
// exited or no permission). std::getline strips the trailing newline.
inline std::string getProcessName(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(f, name);
    return name;
}
