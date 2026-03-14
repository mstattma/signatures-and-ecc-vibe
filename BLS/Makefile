# BLS Signature Stego Channel Demo - Makefile
#
# Usage:
#   make stego_demo CURVE=BLS12-381    # BLS12-381 (~128-bit security)
#   make stego_demo CURVE=BN-158       # BN-158 (~78-bit security)
#   make stego_demo                    # Default: BN-158
#   make build-relic                   # Build RELIC for both curves
#   make clean                         # Remove build artifacts

CC ?= gcc
CFLAGS = -O2 -Wall -Wextra

# Default curve
CURVE ?= BN-158

RELIC_DIR = relic

ifeq ($(CURVE),BLS12-381)
  BUILD_DIR = build-bls12-381
  RELIC_CMAKE_OPTS = -DWSIZE=64 -DRAND=UDEV -DSHLIB=OFF -DSTBIN=ON \
    -DCHECK=off -DVERBS=off -DARITH=x64-asm-6l -DFP_PRIME=381 \
    -DFP_METHD="INTEG;INTEG;INTEG;MONTY;JMPDS;JMPDS;SLIDE" \
    -DCFLAGS="-O3 -funroll-loops -fomit-frame-pointer" \
    -DFP_PMERS=off -DFP_QNRES=on \
    -DFPX_METHD="INTEG;INTEG;LAZYR" \
    -DEP_METHD="JACOB;LWNAF;COMBS;INTER;SWIFT" \
    -DEP_PLAIN=off -DEP_SUPER=off \
    -DPP_METHD="LAZYR;OATEP"
else ifeq ($(CURVE),BN-158)
  BUILD_DIR = build-bn158
  RELIC_CMAKE_OPTS = -DWSIZE=64 -DRAND=UDEV -DSHLIB=OFF -DSTBIN=ON \
    -DCHECK=off -DVERBS=off -DARITH=gmp -DFP_PRIME=158 \
    -DFP_QNRES=off -DBN_PRECI=160 \
    -DFP_METHD="BASIC;COMBA;COMBA;MONTY;JMPDS;JMPDS;SLIDE" \
    -DFPX_METHD="INTEG;INTEG;LAZYR" \
    -DPP_METHD="LAZYR;OATEP" \
    -DEP_METHD="JACOB;LWNAF;COMBS;INTER;SWIFT" \
    -DCFLAGS="-O3 -funroll-loops -fomit-frame-pointer"
else
  $(error Unknown CURVE=$(CURVE). Use BLS12-381 or BN-158)
endif

INCLUDES = -I$(BUILD_DIR)/include -I$(RELIC_DIR)/include
LIBS = -L$(BUILD_DIR)/lib -lrelic_s -lgmp

# Targets

.PHONY: all stego_demo build-relic build-relic-bls12-381 build-relic-bn158 clean test

all: stego_demo

stego_demo: $(BUILD_DIR)/lib/librelic_s.a stego_demo.c bls_stego.c bls_stego.h
	$(CC) $(CFLAGS) $(INCLUDES) -o stego_demo stego_demo.c bls_stego.c $(LIBS)
	@echo ""
	@echo "Built stego_demo for $(CURVE)"
	@echo "Run: ./stego_demo"

test: stego_demo
	./stego_demo

# Build RELIC for both curves
build-relic: build-relic-bls12-381 build-relic-bn158

build-relic-bls12-381:
	@mkdir -p build-bls12-381
	cd build-bls12-381 && cmake $(subst CURVE=BLS12-381,,$(RELIC_CMAKE_OPTS)) \
		-DWSIZE=64 -DRAND=UDEV -DSHLIB=OFF -DSTBIN=ON \
		-DCHECK=off -DVERBS=off -DARITH=x64-asm-6l -DFP_PRIME=381 \
		-DFP_METHD="INTEG;INTEG;INTEG;MONTY;JMPDS;JMPDS;SLIDE" \
		-DCFLAGS="-O3 -funroll-loops -fomit-frame-pointer" \
		-DFP_PMERS=off -DFP_QNRES=on \
		-DFPX_METHD="INTEG;INTEG;LAZYR" \
		-DEP_METHD="JACOB;LWNAF;COMBS;INTER;SWIFT" \
		-DEP_PLAIN=off -DEP_SUPER=off \
		-DPP_METHD="LAZYR;OATEP" ../$(RELIC_DIR) && \
	$(MAKE) -j$$(nproc)

build-relic-bn158:
	@mkdir -p build-bn158
	cd build-bn158 && cmake \
		-DWSIZE=64 -DRAND=UDEV -DSHLIB=OFF -DSTBIN=ON \
		-DCHECK=off -DVERBS=off -DARITH=gmp -DFP_PRIME=158 \
		-DFP_QNRES=off -DBN_PRECI=160 \
		-DFP_METHD="BASIC;COMBA;COMBA;MONTY;JMPDS;JMPDS;SLIDE" \
		-DFPX_METHD="INTEG;INTEG;LAZYR" \
		-DPP_METHD="LAZYR;OATEP" \
		-DEP_METHD="JACOB;LWNAF;COMBS;INTER;SWIFT" \
		-DCFLAGS="-O3 -funroll-loops -fomit-frame-pointer" \
		../$(RELIC_DIR) && \
	$(MAKE) -j$$(nproc)

# Build RELIC for the selected curve only (used by stego_demo target)
$(BUILD_DIR)/lib/librelic_s.a:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake $(RELIC_CMAKE_OPTS) ../$(RELIC_DIR) && $(MAKE) -j$$(nproc)

clean:
	rm -f stego_demo size_test_bls381 size_test_bn158 size_test.c
	rm -rf build-bls12-381 build-bn158
