# ============================================================
# DOCTOR Development Suite - 根目录统一构建
#
# 用法:
#   make           构建全部子项目
#   make test      运行各子项目测试
#   make clean     清理全部构建产物
#   make help      显示帮助
# ============================================================

SUB_DIRS := doctor-sim dasm dcc dlinker dda

.PHONY: all clean test help

all:
	@set -e; \
	for d in $(SUB_DIRS); do \
		echo "===== building $$d ====="; \
		$(MAKE) -C $$d; \
	done
	@echo "===== all projects built ====="

clean:
	@set -e; \
	for d in $(SUB_DIRS); do \
		echo "===== cleaning $$d ====="; \
		$(MAKE) -C $$d clean; \
	done
	@echo "===== all projects cleaned ====="

# 一键运行全部自动化测试（见 run_tests.sh）
test:
	./run_tests.sh

help:
	@echo "Available targets:"
	@echo "  all     - build doctor-sim, dasm, dcc, dlinker, dda"
	@echo "  test    - run subproject test suites"
	@echo "  clean   - clean all build artifacts"
	@echo "  help    - show this help"
