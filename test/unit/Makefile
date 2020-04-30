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
#	Date:		10 April 2020
#	Author:		Alexandre Huff
#
#==================================================================================


CC = gcc

CFLAGS = -g -Wall -I$(INCLUDEDIR)
# LDFLAGS = -lrmr_nng -lnng -lpthread -lm
LDFLAGS = -lrmr_si -lpthread -lm

# DEPFLAGS = -MMD -MP -MT $@ -MF $(DEPDIR)/$*.d
DEPFLAGS = -MMD -MP -MF $(DEPDIR)/$*.d

# FORCE_SYM is required to force gcc to link the rmr library in the compiled program.
# LD returns the message "collect2: error: ld returned 1 exit status" when compiling
# source files which DO NOT call any function either from rmr or pthreads.
# This work around does the trick by forcing the ld to add the rmr_init() symbol,
# thus also adding its respective object file
FORCE_SYM = -u rmr_init

SRCDIR := ../../src
BUILDDIR := ../../.build
INCLUDEDIR := ../../include
DEPDIR := $(BUILDDIR)/deps

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

TESTS := $(wildcard *.c)
TESTS := $(TESTS:%.c=%)
# $(warning $(TESTS))

DEPS := $(OBJS:$(BUILDDIR)/%.o=$(DEPDIR)/%.d)
DEPS += $(TESTS:%=$(DEPDIR)/%.d)
# $(warning $(DEPS))

.PHONY: all clean

.SECONDARY: $(OBJS)

all: $(TESTS)

$(TESTS):

%: %.c $(OBJS)
	$(CC) -o $@ $< $(CFLAGS) $(DEPFLAGS) $(LDFLAGS) $(OBJS) $(FORCE_SYM)

$(BUILDDIR)/%.o : $(SRCDIR)/%.c $(DEPDIR)/%.d | $(DEPDIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# creates the $(BUILDDIR) and $(DEPDIR) directories if they do not exist
$(DEPDIR): ; @mkdir -p $@

# declares each dependency file as a target to avoid make errors
$(DEPS):

clean:
	$(RM) $(TESTS)
	rm -f $(OBJS)
	rm -f $(DEPS)

-include $(wildcard $(DEPS))
