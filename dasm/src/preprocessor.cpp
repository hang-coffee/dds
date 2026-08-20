// preprocessor.cpp
#include "preprocessor.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>     // for realpath
#include <unistd.h>    // for PATH_MAX (通常在 limits.h 中)
#include <limits.h>    // 明确提供 PATH_MAX 的定义
#include <sys/stat.h>

// 如果系统未定义 PATH_MAX，则自己定义（常见值）
#ifndef PATH_MAX
    #define PATH_MAX 4096
#endif

namespace dasm {

// ----- 辅助：去除首尾空白 ------------------------------------------------
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ----- 获取绝对路径（使用 realpath）--------------------------------------
static std::string make_absolute(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) {
        return std::string(resolved);
    }
    // 如果 realpath 失败（文件不存在），返回原路径（相对）
    return path;
}

// ----- 获取父目录 -------------------------------------------------------
static std::string parent_directory(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";   // 无目录部分
    return path.substr(0, pos);
}

// ----- 去除单行注释 ----------------------------------------------------
std::string strip_line_comment(const std::string& line) {
    size_t pos = line.find(';');
    if (pos == std::string::npos) return line;
    return line.substr(0, pos);
}

// ----- 多行注释处理 ----------------------------------------------------
bool process_multiline_comment(
    const std::string& line,
    bool currently_in_comment,
    std::string& out_cleaned_line
) {
    out_cleaned_line.clear();
    std::string processed = line;

    if (currently_in_comment) {
        size_t end_pos = processed.find("*/");
        if (end_pos == std::string::npos) {
            return true;   // 仍在注释中
        } else {
            processed = processed.substr(end_pos + 2);
            currently_in_comment = false;
        }
    }

    // 不在注释中，处理 /* ... */
    size_t start_pos = processed.find("/*");
    while (start_pos != std::string::npos) {
        size_t end_pos = processed.find("*/", start_pos + 2);
        if (end_pos == std::string::npos) {
            processed = processed.substr(0, start_pos);
            out_cleaned_line = trim(processed);
            return true;   // 进入多行注释
        } else {
            processed.erase(start_pos, end_pos - start_pos + 2);
            start_pos = processed.find("/*");
        }
    }

    out_cleaned_line = trim(processed);
    return false;
}

// ----- 解析 %define ----------------------------------------------------
bool parse_define_directive(const std::string& line, macro_table& macros) {
    std::string rest = line.substr(8); // 跳过 "%define"
    rest = trim(rest);
    if (rest.empty()) return false;

    size_t first_space = rest.find_first_of(" \t");
    if (first_space == std::string::npos) return false;

    std::string name = trim(rest.substr(0, first_space));
    std::string replacement = trim(rest.substr(first_space + 1));

    if (!name.empty()) {
        macros.definitions[name] = replacement;
        return true;
    }
    return false;
}

// ----- 解析 %include ----------------------------------------------------
std::string parse_include_directive(const std::string& line) {
    size_t start = line.find('"');
    if (start == std::string::npos) start = line.find('\'');
    if (start == std::string::npos) return "";

    size_t end = line.find(line[start], start + 1);
    if (end == std::string::npos) return "";

    return line.substr(start + 1, end - start - 1);
}

// ----- 宏展开（简单替换）-----------------------------------------------
std::string expand_macros(const std::string& line, const macro_table& macros) {
    std::string result = line;
    for (const auto& pair : macros.definitions) {
        const std::string& name = pair.first;
        const std::string& repl = pair.second;
        size_t pos = 0;
        while ((pos = result.find(name, pos)) != std::string::npos) {
            // 检查是否是一个完整的单词（避免误替换子串）
            bool left_ok  = (pos == 0 || (!isalnum(result[pos - 1]) && result[pos - 1] != '_'));
            bool right_ok = (pos + name.size() == result.size() ||
                             (!isalnum(result[pos + name.size()]) && result[pos + name.size()] != '_'));
            if (left_ok && right_ok) {
                result.replace(pos, name.size(), repl);
                pos += repl.size();
            } else {
                pos += name.size();
            }
        }
    }
    return result;
}

// ----- 主预处理函数 ----------------------------------------------------
std::vector<std::pair<int, std::string>> preprocess_file(
    const std::string& file_path,
    preprocessor_context& ctx
) {
    // 1. 转为绝对路径（防止重复包含）
    std::string abs_path = make_absolute(file_path);
    if (ctx.included_files.find(abs_path) != ctx.included_files.end()) {
        return {};   // 已包含，跳过
    }
    ctx.included_files.insert(abs_path);

    // 2. 打开文件
    std::ifstream ifs(abs_path);
    if (!ifs.is_open()) {
        // 错误处理：文件不存在（可增加错误收集，这里简单返回空）
        return {};
    }

    std::vector<std::pair<int, std::string>> output_lines;
    bool in_multiline_comment = false;

    std::string line;
    int line_no = 0;
    while (std::getline(ifs, line)) {
        line_no++;
        // 3. 多行注释
        std::string cleaned;
        in_multiline_comment = process_multiline_comment(
            line, in_multiline_comment, cleaned
        );
        if (cleaned.empty() && in_multiline_comment) continue;
        if (cleaned.empty()) continue;

        // 4. 单行注释
        cleaned = strip_line_comment(cleaned);
        cleaned = trim(cleaned);
        if (cleaned.empty()) continue;

        // 5. 伪指令处理
        // 5a. %define
        if (cleaned.compare(0, 8, "%define ") == 0) {
            parse_define_directive(cleaned, ctx.macros);
            continue;
        }

        // 5b. %include
        if (cleaned.compare(0, 8, "%include ") == 0) {
            std::string inc_file = parse_include_directive(cleaned);
            if (!inc_file.empty()) {
                // 构造包含文件的绝对路径：相对于当前源文件所在目录
                std::string current_dir = parent_directory(abs_path);
                std::string full_inc_path = current_dir + "/" + inc_file;
                // 递归处理（内部会进行绝对路径归一化）
                auto included_lines = preprocess_file(full_inc_path, ctx);
                output_lines.insert(output_lines.end(),
                                    included_lines.begin(),
                                    included_lines.end());
            }
            continue;
        }

        // 6. 宏展开
        cleaned = expand_macros(cleaned, ctx.macros);
        cleaned = trim(cleaned);
        if (cleaned.empty()) continue;

        // 7. 保留有效行（携带源文件真实行号）
        output_lines.push_back({line_no, cleaned});
    }

    return output_lines;
}

} // namespace dasm
