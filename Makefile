# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later

# Makefile for Atari Lynx Pong Wars
# Needs cc65 (https://cc65.github.io) and Python 3, standard library only

CL65 := cl65
PYTHON := python3
CFLAGS := -t lynx -O

SRC_DIR := src
BUILD_DIR := build
DIST_DIR := dist

CART := $(BUILD_DIR)/pongwars.lnx
TARGET := $(DIST_DIR)/pongwars.lnx
SPLASH := $(SRC_DIR)/splash.png
OBJS := $(BUILD_DIR)/pongwars.o $(BUILD_DIR)/exehdr.o $(BUILD_DIR)/splash.o

# Rules

all: $(TARGET)

$(TARGET): $(CART)
	mkdir -p $(DIST_DIR)
	cp $< $@

$(CART): $(OBJS)
	@echo "Building $@..."
	$(CL65) -t lynx -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(CL65) -t lynx -c -o $@ $<

$(BUILD_DIR)/splash.o: $(BUILD_DIR)/splash.c
	$(CL65) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/splash.c: $(SPLASH) tools/png2lynx.py | $(BUILD_DIR)
	$(PYTHON) tools/png2lynx.py $(SPLASH) $@ splash

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

.PHONY: all clean
