#==================================================================================
#	Copyright (c) 2020 AT&T Intellectual Property.
#	Copyright (c) 2020 Alexandre Huff.
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

CFLAGS = -g -Wall -O3 -I$(INCLUDEDIR) -DLOGGER_LEVEL=LOGGER_INFO
LDFLAGS = -lrmr_si -lpthread -lm

# DEPFLAGS = -MMD -MP -MT $@ -MF $(DEPDIR)/$*.d
DEPFLAGS = -MMD -MP -MF $(DEPDIR)/$*.d

SRCDIR := ../../src
BUILDDIR := ../../.build
INCLUDEDIR := ../../include
DEPDIR := $(BUILDDIR)/deps

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

APPS := $(wildcard *.c)
APPS := $(APPS:%.c=%)
# $(warning $(APPS))

DEPS := $(OBJS:$(BUILDDIR)/%.o=$(DEPDIR)/%.d)
DEPS += $(APPS:%=$(DEPDIR)/%.d)
# $(warning $(DEPS))

.PHONY: all clean

.SECONDARY: $(OBJS)

all: $(APPS)

$(APPS):

%: %.c $(OBJS)
	$(CC) -o $@ $< $(CFLAGS) $(DEPFLAGS) $(LDFLAGS) $(OBJS)

$(BUILDDIR)/%.o : $(SRCDIR)/%.c $(DEPDIR)/%.d | $(DEPDIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# creates the $(BUILDDIR) and $(DEPDIR) directories if they do not exist
$(DEPDIR): ; @mkdir -p $@

# declares each dependency file as a target to avoid make errors if they do not exist
$(DEPS):

clean:
	$(RM) $(APPS)
	rm -f $(OBJS)
	rm -f $(DEPS)

-include $(wildcard $(DEPS))
