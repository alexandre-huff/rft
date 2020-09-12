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
#	Date:		7 April 2020
#	Author:		Alexandre Huff
#
#	Abstract:	This Makefile is an example of how to compile and link a program
#				with the RFT library.
#				NOTE: Install RFT library in the system before run this Makefile
#
#==================================================================================


CC = gcc
# use -DNORFT to disable RFT in compilation time
CFLAGS = -g -Wall
# LDFLAGS = -lrmr_nng -lnng -lpthread -lm
LDFLAGS = -lrmr_si -lpthread -lm
RFTFLAG = -lrft

PROGS = $(wildcard *.c)
PROGS := $(patsubst %.c,%,$(PROGS))

all: $(PROGS)

# linking rft only on xapps
xapp: xapp.c examples.h
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) $(RFTFLAG)

# linking rft only on xapps
xapp2: xapp2.c examples.h
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) $(RFTFLAG)

sender: sender.c examples.h
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

sender2: sender2.c examples.h
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(PROGS)
