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

# 运行各子项目已有的测试入口。
# doctor-sim 没有统一 test 目标，因此直接调用其测试脚本。
test:
	@set -e; \
	echo "===== dasm test ====="; \
	$(MAKE) -C dasm test; \
	echo "===== dcc test ====="; \
	$(MAKE) -C dcc test; \
	echo "===== dlinker test ====="; \
	$(MAKE) -C dlinker test; \
	echo "===== doctor-sim tests ====="; \
	sh doctor-sim/tests/run_dasm_test.sh; \
	sh doctor-sim/tests/run_irq_test.sh; \
	sh doctor-sim/tests/run_display_test.sh
	@echo "===== all tests passed ====="

help:
	@echo "Available targets:"
	@echo "  all     - build doctor-sim, dasm, dcc, dlinker, dda"
	@echo "  test    - run subproject test suites"
	@echo "  clean   - clean all build artifacts"
	@echo "  help    - show this help"
