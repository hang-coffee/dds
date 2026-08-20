// typeenv.h - dcc 用户定义类型环境（struct/union/enum/typedef）
//
// parser 填充定义与布局，codegen 查询成员偏移/尺寸/枚举常量。
// Type 的 tname 字段引用这里的定义名（struct/union 标签 或 typedef 名）。

#ifndef DCC_TYPEENV_H
#define DCC_TYPEENV_H

#include "ast.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dcc {

struct MemberDef {
	std::string name;
	Type type;				// 成员类型（含指针/数组标记）
	bool is_array;			// 数组成员
	int array_len;			// 数组元素个数（is_array 时）
	uint32_t offset;		// 布局后相对结构体起点的字节偏移
};

struct StructDef {
	std::string name;		// 标签名（空 = 匿名）
	bool is_union;			// true=union（成员共享存储）
	std::vector<MemberDef> members;
	uint32_t size;			// 布局后总大小
};

struct EnumDef {
	std::string name;		// 标签名（空 = 匿名）
	std::vector<std::pair<std::string, long long>> constants;
};

// 类型环境：struct/union/enum/typedef 表
class TypeEnv {
public:
	// struct/union
	const StructDef* lookup_struct(const std::string& name) const {
		auto it = structs_.find(name);
		return it == structs_.end() ? nullptr : &it->second;
	}
	void add_struct(StructDef&& d) { structs_[d.name] = std::move(d); }
	bool has_struct(const std::string& name) const { return structs_.count(name) != 0; }

	// enum（返回常量值；-1 表示未找到）
	const EnumDef* lookup_enum(const std::string& name) const {
		auto it = enums_.find(name);
		return it == enums_.end() ? nullptr : &it->second;
	}
	void add_enum(EnumDef&& d) { enums_[d.name] = std::move(d); }

	// typedef
	const Type* lookup_typedef(const std::string& name) const {
		auto it = typedefs_.find(name);
		return it == typedefs_.end() ? nullptr : &it->second;
	}
	void add_typedef(const std::string& name, const Type& t) { typedefs_[name] = t; }
	bool has_typedef(const std::string& name) const { return typedefs_.count(name) != 0; }

	// 枚举常量查找（-1 表示未找到）
	long long enum_value(const std::string& name) const {
		for (const auto& e : enums_) {
			for (const auto& kv : e.second.constants)
				if (kv.first == name) return kv.second;
		}
		return -1;
	}

private:
	std::unordered_map<std::string, StructDef> structs_;
	std::unordered_map<std::string, EnumDef> enums_;
	std::unordered_map<std::string, Type> typedefs_;
};

} // namespace dcc

#endif
