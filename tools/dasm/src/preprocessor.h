#ifndef DASM_PREPROCESSOR_H
#define DASM_PREPROCESSOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace dasm {

// ---------- 纯数据容器 ----------
struct macro_table {
    // 宏名 -> 替换文本（字符串，保留原样，后续词法分析再解析）
    std::unordered_map<std::string, std::string> definitions;
};

struct preprocessor_context {
    macro_table macros;
    std::unordered_set<std::string> included_files;   // 已包含文件的绝对路径
    std::string current_source_dir;                   // 当前正在处理的文件所在目录
};

// ---------- 主入口 ----------
// 预处理一个文件，返回清理后的有效代码行列表（每条都是非空且无注释）。
// 每个元素为 (源文件行号, 行文本)，行号用于错误报告（预处理会删除空行/注释行，
// 若用预处理后的行号会与源文件对不上）。
std::vector<std::pair<int, std::string>> preprocess_file(
    const std::string& file_path,
    preprocessor_context& ctx
);

// ---------- 辅助函数（暴露给测试，但在 .cpp 中通常标记为 static） ----------
// 去除单行注释（; 之后的内容），返回清理后的行
std::string strip_line_comment(const std::string& line);

// 处理多行注释状态，返回是否仍在注释中，并输出有效内容
bool process_multiline_comment(
    const std::string& line,
    bool currently_in_comment,
    std::string& out_cleaned_line
);

// 展开一行的宏（从左到右替换，简单文本替换）
std::string expand_macros(
    const std::string& line,
    const macro_table& macros
);

// 解析 %define 定义，填充 macro_table
bool parse_define_directive(
    const std::string& line,
    macro_table& macros
);

// 解析 %include，返回包含的文件名（去掉引号）
std::string parse_include_directive(const std::string& line);

} // namespace dasm

#endif
