#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rosetta::detail {

inline std::string trim_type(const std::string& text) {
    const auto start = text.find_first_not_of(" \n\r\t");
    if (start == std::string::npos) return {};
    return text.substr(start, text.find_last_not_of(" \n\r\t") - start + 1);
}

struct FunctionSpelling {
    std::string result;
    std::string parameters;
    std::string suffix;
};

// This interprets Clang's normalized type spelling, not C++ source. Keep it
// isolated until return/function types come directly from a compiler API.
inline std::optional<FunctionSpelling> function_spelling(const std::string& text) {
    int angle = 0;
    for (std::size_t open = 0; open < text.size(); ++open) {
        if (text[open] == '<') ++angle;
        else if (text[open] == '>') --angle;
        else if (text[open] == '(' && angle == 0) {
            int parens = 1;
            for (std::size_t close = open + 1; close < text.size(); ++close) {
                if (text[close] == '(') ++parens;
                if (text[close] == ')' && --parens == 0)
                    return FunctionSpelling{trim_type(text.substr(0, open)), text.substr(open + 1, close - open - 1),
                        trim_type(text.substr(close + 1))};
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

inline std::vector<std::string> type_arguments(const std::string& text) {
    if (trim_type(text).empty()) return {};
    std::vector<std::string> result;
    std::size_t start = 0;
    int angle = 0, parens = 0, brackets = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || (text[i] == ',' && angle == 0 && parens == 0 && brackets == 0)) {
            result.push_back(trim_type(text.substr(start, i - start)));
            start = i + 1;
        } else if (text[i] == '<') ++angle;
        else if (text[i] == '>') --angle;
        else if (text[i] == '(') ++parens;
        else if (text[i] == ')') --parens;
        else if (text[i] == '[') ++brackets;
        else if (text[i] == ']') --brackets;
    }
    return result;
}

} // namespace rosetta::detail
