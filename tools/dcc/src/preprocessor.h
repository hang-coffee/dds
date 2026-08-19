// preprocessor.h - dcc 预处理器（#include / #define 等）
#ifndef DCC_PREPROCESSOR_H
#define DCC_PREPROCESSOR_H

#include <string>
#include <vector>

namespace dcc {

// 预处理结果
struct PreprocessResult {
	std::string text;				// 宏展开/注释剥离后的完整文本
	std::vector<std::string> errs;	// 错误："文件:行: 消息"
	std::vector<std::string> includes;	// 被实际展开的 #include 头文件 basename（去扩展名，去重）
	bool ok() const { return errs.empty(); }
};

// 预处理：处理 #include / #define / #undef / #ifdef / #ifndef / #else / #endif，
// 展开对象宏与函数宏，剥离注释（保留换行）。
//   src       源文件内容
//   filename  源文件名（用于错误信息与 "..." 相对查找）
//   include_dir  `<foo.h>` 的默认查找目录（如 项目根/tools/dcc/include）；
//               `"foo.h"` 先找当前文件目录，再回退 include_dir。
PreprocessResult preprocess(const std::string& src,
                            const std::string& filename,
                            const std::string& include_dir);

} // namespace dcc

#endif
