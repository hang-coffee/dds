// ast.cpp - AST 析构与符号表
#include "ast.h"

namespace dcc {

Expr::~Expr() {
	delete l;
	delete r;
	delete c;
	for (Expr* a : args) delete a;
}

Stmt::~Stmt() {
	delete expr;
	delete cond;
	delete then;
	delete els;
	delete body;
	delete init;
	delete init_stmt;
	delete inc;
	for (Stmt* s : items) delete s;
	delete next;
}

void SymTable::push_scope() {
	scopes_.emplace_back();
}

void SymTable::pop_scope() {
	if (!scopes_.empty()) scopes_.pop_back();
}

bool SymTable::declare(const Symbol& s) {
	if (scopes_.empty()) scopes_.emplace_back();
	auto& scope = scopes_.back();
	if (scope.find(s.name) != scope.end()) return false;
	scope[s.name] = s;
	return true;
}

const Symbol* SymTable::lookup(const std::string& name) const {
	for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
		auto f = it->find(name);
		if (f != it->end()) return &f->second;
	}
	return nullptr;
}

} // namespace dcc
