# Unified Stego Signature Demo - Makefile
#
# Usage:
#   make stego_demo SCHEME=uov-80       # UOV, 80-bit security
#   make stego_demo SCHEME=uov-100      # UOV, 100-bit security
#   make stego_demo SCHEME=bls-bn158    # BLS BN-P158, ~78-bit security
#   make stego_demo SCHEME=bls12-381    # BLS12-381, ~120-bit security
#   make test SCHEME=...                # Build and run
#   make clean

CC ?= gcc
# Note: -O1 instead of -O2 due to a RELIC crash with BLS12-381 at -O2.
# RELIC's core_init triggers a stack smash at -O2 in the BLS12-381 build.
# BN-158 and UOV work fine at -O2. This appears to be a RELIC/compiler interaction bug.
CFLAGS = -O1 -Wall -Wextra -std=c99

SCHEME ?= bls-bn158

UOV_BASE   = ../UOV/pqov
BLS_DIR    = ../BLS
RELIC_DIR  = $(BLS_DIR)/relic

# ====================================================================
# UOV schemes
# ====================================================================
ifeq ($(SCHEME),uov-80)
  BACKEND = uov
  UOV_PARAM_DEFINE = -D_OV256_50_20
endif
ifeq ($(SCHEME),uov-100)
  BACKEND = uov
  UOV_PARAM_DEFINE = -D_OV256_63_25
endif

# ====================================================================
# BLS schemes
# ====================================================================
ifeq ($(SCHEME),bls-bn158)
  BACKEND = bls
  BLS_BUILD = $(BLS_DIR)/build-bn158
  BLS_CURVE = BN-158
endif
ifeq ($(SCHEME),bls12-381)
  BACKEND = bls
  BLS_BUILD = $(BLS_DIR)/build-bls12-381
  BLS_CURVE = BLS12-381
endif

ifndef BACKEND
  $(error Unknown SCHEME=$(SCHEME). Use: uov-80, uov-100, bls-bn158, bls12-381)
endif

# ====================================================================
# Backend: UOV
# ====================================================================
ifeq ($(BACKEND),uov)

UOV_SRC   = $(UOV_BASE)/src
UOV_REF   = $(UOV_BASE)/src/ref
UOV_UTIL  = $(UOV_BASE)/utils

UOV_CSRCS = $(wildcard $(UOV_SRC)/*.c) \
            $(wildcard $(UOV_REF)/*.c) \
            $(wildcard $(UOV_UTIL)/*.c)

INCLUDES  = -I$(UOV_SRC) -I$(UOV_REF) -I$(UOV_UTIL) -I.
DEFINES   = $(UOV_PARAM_DEFINE) -D_OV_CLASSIC
LIBS      = -lcrypto
BACKEND_SRC = stego_sig_uov.c

stego_demo: stego_demo.c $(BACKEND_SRC) stego_sig.h $(UOV_CSRCS)
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ stego_demo.c $(BACKEND_SRC) $(UOV_CSRCS) $(LIBS)
	@echo ""
	@echo "Built stego_demo for $(SCHEME)"

endif

# ====================================================================
# Backend: BLS
# ====================================================================
ifeq ($(BACKEND),bls)

INCLUDES    = -I$(BLS_BUILD)/include -I$(RELIC_DIR)/include -I.
LIBS        = -L$(BLS_BUILD)/lib -lrelic_s -lgmp
BACKEND_SRC = stego_sig_bls.c

# Build RELIC if needed
$(BLS_BUILD)/lib/librelic_s.a:
	$(MAKE) -C $(BLS_DIR) build-relic-$(if $(findstring bn158,$(SCHEME)),bn158,bls12-381)

stego_demo: $(BLS_BUILD)/lib/librelic_s.a stego_demo.c $(BACKEND_SRC) stego_sig.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ stego_demo.c $(BACKEND_SRC) $(LIBS)
	@echo ""
	@echo "Built stego_demo for $(SCHEME)"

endif

# ====================================================================
# Common targets
# ====================================================================
.PHONY: test clean all stardust_payload_demo

all: stego_demo

stardust_payload_demo: $(if $(filter $(BACKEND),uov),stego_demo.c $(BACKEND_SRC) stego_sig.h $(UOV_CSRCS),$(BLS_BUILD)/lib/librelic_s.a stego_sig.h $(BACKEND_SRC)) stardust_payload_demo.c
ifeq ($(BACKEND),uov)
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ stardust_payload_demo.c $(BACKEND_SRC) $(UOV_CSRCS) $(LIBS)
else
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ stardust_payload_demo.c $(BACKEND_SRC) $(LIBS)
endif

test: stego_demo
	./stego_demo

clean:
	rm -f stego_demo stardust_payload_demo
