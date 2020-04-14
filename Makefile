#==================================================================================
#	Copyright (c) 2019-2020 AT&T Intellectual Property.
#
#	Licensed under the Apache License, Version 2.0 (the "License");
#	you may not use this file except in compliance with the License.
#	You may obtain a copy of the License at
#
#		http://www.apache.org/licenses/LICENSE-2.0
#
#	Unless required by applicable law or agreed to in writing, software
#	distributed under the License is distributed on an "AS IS" BASIS,
#	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#	See the License for the specific language governing permissions and
#	limitations under the License.
#==================================================================================
#
#	Abstract:	This makefile only compiles the RFT library. Programs that want
#				fault tolerance must be linked with this library
#
#	Date:		9 April 2020
#	Author:		Alexandre Huff
#
#==================================================================================


PREFIX = /usr/local

CC = gcc

CFLAGS = -g -Wall -I$(INCLUDEDIR)
DEPFLAGS = -MMD -MP -MT $@ -MF $(DEPDIR)/$*.d

# install files
SNAME = librft.a
DNAME = librft.so
HNAME = rft.h
HLOGR = logger.h

# LOGGER_LEVEL = -DLOGGER_LEVEL=LOGGER_ERROR

SRCDIR = src
BUILDDIR = .build
INCLUDEDIR = include
DEPDIR := $(BUILDDIR)/deps

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
DEPS := $(OBJS:$(BUILDDIR)/%.o=$(DEPDIR)/%.d)

.PHONY: all dev static shared install uninstall clean distclean

all: dev

# compiles the static library for development
dev: $(SNAME)

# compiles the static library removing assertion
static: CFLAGS += -DNDEBUG
static: $(SNAME)

# compiling with position-independent code and removing assertion
shared: CFLAGS += -fPIC -DNDEBUG
shared: $(DNAME)

$(SNAME): $(OBJS)
	ar rcvs $(SNAME) $(OBJS)

$(DNAME): $(OBJS)
	$(CC) -shared -o $(DNAME) $(OBJS)

$(BUILDDIR)/%.o : $(SRCDIR)/%.c $(DEPDIR)/%.d | $(DEPDIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $< $(LOGGER_LEVEL)

# creates the $(BUILDDIR) and $(DEPDIR) directories if they do not exist
$(DEPDIR): ; @mkdir -p $@

# declares each dependency file as a target to avoid make errors if they do not exist
$(DEPS):

install:
ifneq ($(wildcard $(DNAME)),)
	install -d $(PREFIX)/lib/
	install $(DNAME) $(PREFIX)/lib/
	@echo Please, export LD_LIBRARY_PATH=$(PREFIX)/lib:$$LD_LIBRARY_PATH to load RFT
else
ifneq ($(wildcard $(SNAME)),)
	install -d $(PREFIX)/lib/
	install -d $(PREFIX)/include/rft/
	install -m 644 $(SNAME) $(PREFIX)/lib/
	install -m 644 $(INCLUDEDIR)/$(HNAME) $(INCLUDEDIR)/$(HLOGR) $(PREFIX)/include/rft/
endif
endif

uninstall:
	rm -f $(PREFIX)/lib/$(DNAME)
	rm -f $(PREFIX)/lib/$(SNAME)
ifneq ($(wildcard $(PREFIX)/include/rft),)
	rm -f $(PREFIX)/include/rft/*
	rmdir $(PREFIX)/include/rft/
endif

clean:
	rm -f $(OBJS)
	rm -f $(DEPS)
	rm -f $(SNAME)
	rm -f $(DNAME)

distclean: clean
ifneq ($(wildcard $(BUILDDIR)),)
	rmdir $(DEPDIR)
	rmdir $(BUILDDIR)
endif

-include $(wildcard $(DEPS))
