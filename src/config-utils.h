#ifndef CONFIG_UTILS_H
#define CONFIG_UTILS_H

#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <climits>
#include <cstdlib>

// Parse integer from string with range validation, returns true on success
static inline bool parse_int(const std::string &s, long &out, long min_val, long max_val)
{
    char *end = nullptr;
    long val = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || val < min_val || val > max_val)
        return false;
    out = val;
    return true;
}

// Safe string-to-int with full-string validation and range check
static inline int safe_stoi(const std::string &s, int fallback, int min_val = INT_MIN, int max_val = INT_MAX)
{
    char *end = nullptr;
    long val = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') {
        std::cerr << "Warning: invalid integer '" << s << "', using " << fallback << "\n";
        return fallback;
    }
    if (val < min_val || val > max_val) {
        std::cerr << "Warning: value " << val << " out of range [" << min_val << "," << max_val << "], using " << fallback << "\n";
        return fallback;
    }
    return (int)val;
}

// Safe string-to-float with full-string validation
static inline float safe_stof(const std::string &s, float fallback)
{
    char *end = nullptr;
    float val = strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') {
        std::cerr << "Warning: invalid float '" << s << "', using " << fallback << "\n";
        return fallback;
    }
    if (val < 0.01f || val > 100.0f) {
        std::cerr << "Warning: float " << val << " out of range [0.01,100], using " << fallback << "\n";
        return fallback;
    }
    return val;
}

// Read key=value config file, skipping comments and blank lines
static inline std::map<std::string, std::string> read_config(const std::string &path)
{
    std::map<std::string, std::string> cfg;
    std::ifstream file(path);
    if (!file.is_open())
        return cfg;
    std::string line;
    while (std::getline(file, line))
    {
        // trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#')
            continue;
        size_t eq = line.find('=', start);
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(start, eq - start);
        // trim trailing whitespace from key
        size_t kend = key.find_last_not_of(" \t");
        if (kend != std::string::npos)
            key = key.substr(0, kend + 1);
        std::string val = line.substr(eq + 1);
        // trim leading and trailing whitespace from value
        size_t vstart = val.find_first_not_of(" \t");
        size_t vend = val.find_last_not_of(" \t\r\n");
        if (vstart != std::string::npos && vend != std::string::npos)
            val = val.substr(vstart, vend - vstart + 1);
        else
            val.clear();
        cfg[key] = val;
    }
    return cfg;
}

#endif // CONFIG_UTILS_H
