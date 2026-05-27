# Root Makefile — orchestrates the WASI Python-wheels build.
#
# Subdirectories each have their own self-contained Makefile:
#   cpython/         WASI cross-build of CPython 3.14 (libpython3.14.so
#                    + headers; consumed by the wheel builds, not a
#                    wheel itself).
#   libffi-wasi/     libffi static lib for wasm32-wasip2 (consumed by
#                    cffi, not a wheel itself).
#   cffi/            cffi wheel (cp314, wasm32-wasip2).
#   cryptography/    cryptography wheel (cp314, wasm32-wasip2).
#   pydantic-core/   pydantic-core wheel (cp314, wasm32-wasip2).
#   regex/           regex wheel (mrab-regex; cp314, wasm32-wasip2).
#   pyyaml/          PyYAML wheel + libyaml.a cross-build (cp314,
#                    wasm32-wasip2).
#
# This file:
#   1. Drives the four sub-builds in dependency order.
#   2. Collects the produced wheels into ./dist/.
#   3. Emits a PEP 503 simple-repository index under ./dist/simple/.
#
# After a successful build, pip can install from the result via:
#
#   pip install \
#     --index-url file://$(pwd)/dist/simple/ \
#     cffi cryptography pydantic-core
#
# Targets:
#   make             # equivalent to `make all`
#   make all         # build everything, collect wheels, emit index
#   make wheels      # build wheels + copy into ./dist/  (no index)
#   make index       # (re)generate ./dist/simple/ from ./dist/*.whl
#   make <component> # build a single component (e.g. `make cffi`)
#   make clean       # rm -rf ./dist/ and recurse `clean` into subdirs

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

DIST_DIR  := $(THIS_DIR)dist
INDEX_DIR := $(DIST_DIR)/simple

CPYTHON_DIR       := $(THIS_DIR)cpython
LIBFFI_DIR        := $(THIS_DIR)libffi-wasi
CFFI_DIR          := $(THIS_DIR)cffi
CRYPTOGRAPHY_DIR  := $(THIS_DIR)cryptography
PYDANTIC_CORE_DIR := $(THIS_DIR)pydantic-core
REGEX_DIR         := $(THIS_DIR)regex
PYYAML_DIR        := $(THIS_DIR)pyyaml

# ---- top-level entry points ---------------------------------------------

.PHONY: all wheels index clean \
        cpython libffi-wasi cffi cryptography pydantic-core regex pyyaml

all: index

# Each component target just recurses into its subdir; the child
# Makefile is authoritative for up-to-date-ness, so making these
# phony means we always delegate the decision.
cpython:
	$(MAKE) -C $(CPYTHON_DIR)

libffi-wasi:
	$(MAKE) -C $(LIBFFI_DIR)

# cffi needs both the wasm CPython (headers) and our libffi port.
cffi: cpython libffi-wasi
	$(MAKE) -C $(CFFI_DIR)

# cryptography only needs the wasm CPython (libpython.so + headers);
# it brings its own AWS-LC + host-python cffi codegen.
cryptography: cpython
	$(MAKE) -C $(CRYPTOGRAPHY_DIR)

# pydantic-core is pure Rust + PyO3 against the wasm CPython — no C deps,
# no host-python codegen.
pydantic-core: cpython
	$(MAKE) -C $(PYDANTIC_CORE_DIR)

# regex is a hand-compiled C extension against the wasm CPython —
# closest in shape to cffi (no setuptools, manual wheel assembly).
regex: cpython
	$(MAKE) -C $(REGEX_DIR)

# pyyaml builds libyaml.a (autotools, cross-compiled), runs Cython on
# _yaml.pyx, then compiles + links the resulting _yaml.c — heaviest of
# the hand-built wheels.
pyyaml: cpython
	$(MAKE) -C $(PYYAML_DIR)

# ---- wheel collection ---------------------------------------------------
#
# Each subdir drops its wheel into <subdir>/out/. Copy whatever lands
# there into ./dist/. The .collected sentinel sequences `index` after
# the copy; the component deps above guarantee the wheels exist first.

wheels: $(DIST_DIR)/.collected

$(DIST_DIR)/.collected: cffi cryptography pydantic-core regex pyyaml | $(DIST_DIR)
	cp $(CFFI_DIR)/out/*.whl $(DIST_DIR)/
	cp $(CRYPTOGRAPHY_DIR)/out/*.whl $(DIST_DIR)/
	cp $(PYDANTIC_CORE_DIR)/out/*.whl $(DIST_DIR)/
	cp $(REGEX_DIR)/out/*.whl $(DIST_DIR)/
	cp $(PYYAML_DIR)/out/*.whl $(DIST_DIR)/
	cd $(DIST_DIR) && sha256sum *.whl > SHA256SUMS
	@touch $@
	@echo "✓  wheels in $(DIST_DIR)/:"
	@ls -lh $(DIST_DIR)/*.whl | awk '{print "    "$$5"  "$$NF}'

$(DIST_DIR):
	mkdir -p $@

# ---- PEP 503 simple index -----------------------------------------------
#
# Layout produced:
#   dist/
#     <wheel files>
#     simple/
#       index.html                -- root listing (one <a> per package)
#       <pkg>/index.html          -- per-package listing with sha256
#
# pip is told to use file://.../dist/simple/ as its index URL; the
# per-package pages href="../../<filename>#sha256=…" resolves back to
# dist/<filename>.
#
# The package name is taken as the first dash-separated field of the
# wheel filename (PEP 427). cffi / cryptography are already in PEP 503
# normalized form, so no extra normalization is applied; if you ever
# add a wheel whose name uses '_' or mixed case, normalize first via
# `tr '[:upper:]' '[:lower:]' | sed 's/[-_.]\+/-/g'`.

index: $(DIST_DIR)/.collected
	@rm -rf $(INDEX_DIR)
	@mkdir -p $(INDEX_DIR)
	@pkgs=$$(ls $(DIST_DIR)/*.whl 2>/dev/null | xargs -n1 basename \
	         | awk -F- '{print $$1}' | sort -u); \
	 { echo '<!DOCTYPE html>'; \
	   echo '<html><head><meta name="pypi:repository-version" content="1.0"></head><body>'; \
	   for p in $$pkgs; do \
	     printf '  <a href="%s/">%s</a><br>\n' "$$p" "$$p"; \
	   done; \
	   echo '</body></html>'; \
	 } > $(INDEX_DIR)/index.html; \
	 for p in $$pkgs; do \
	   mkdir -p $(INDEX_DIR)/$$p; \
	   { echo '<!DOCTYPE html>'; \
	     echo '<html><head><meta name="pypi:repository-version" content="1.0"></head><body>'; \
	     for w in $(DIST_DIR)/$${p}-*.whl; do \
	       wn=$$(basename $$w); \
	       sha=$$(sha256sum $$w | awk '{print $$1}'); \
	       printf '  <a href="../../%s#sha256=%s">%s</a><br>\n' \
	              "$$wn" "$$sha" "$$wn"; \
	     done; \
	     echo '</body></html>'; \
	   } > $(INDEX_DIR)/$$p/index.html; \
	 done
	@echo "✓  PEP 503 index at $(INDEX_DIR)/"
	@echo "   pip install --index-url file://$(abspath $(INDEX_DIR))/ <pkg>"

# ---- clean ---------------------------------------------------------------

clean:
	rm -rf $(DIST_DIR)
	-$(MAKE) -C $(CPYTHON_DIR) clean
	-$(MAKE) -C $(LIBFFI_DIR) clean
	-$(MAKE) -C $(CFFI_DIR) clean
	-$(MAKE) -C $(CRYPTOGRAPHY_DIR) clean
	-$(MAKE) -C $(PYDANTIC_CORE_DIR) clean
	-$(MAKE) -C $(REGEX_DIR) clean
	-$(MAKE) -C $(PYYAML_DIR) clean
