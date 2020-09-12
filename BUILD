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
#	Date:		14 April 2020
#	Author:		Alexandre Huff
#
#==================================================================================


Building RFT

The RIC Fault Tolerance (RFT) can be built with make and requires a modern C compiler.
Usually, the following packages are requird to build the RFT library:

	make gcc git

The git package is only required to build the library in contairnerized environments.
To build the RFT, the following steps are required to be executed on the RFT's root directory:

	$ make
	$ sudo make install

This will build and install the development environment by installing a static library and
its header files to the default directory /usr/local/lib and /usr/local/include/rft respectively.
Change the PREFIX variable in the Makefile to modify the default install directory.
There are other build options which are described in the following:

	$ make dev
		This is the default option which is executed when no argument is passed to the make command.

	$ make static
		This will compile the static library .a as in make dev, but will disable assertion checks
		in the RFT code. This is the prefered build method for production systems.

	$ make shared
		This will compile the shared library .so  with position-independent code and also disables
		assertion checks in the RFT code.
		NOTE: $ make install command won't install header files with the .so file.

To uninstall the RFT library run the following command:

	$ sudo make uninstall

To clean the build environment run the following commands:

	$ make clean
		Removes all object and dependencies files, as well as the compiled library

	$ make distclean
		Removes all files and directories created during the build process

Linking RFT

The RFT library can be linked with the -lrft option. However, RFT depends on the RMR library which is the
RIC Message Router used to exchange messages between the RFT nodes.
Thus, the typical libraries required to be linked by the user application are the following:

	-lrmr_si -lpthread -lm -lrft
	or
	-lrmr_nng -lnng -lpthread -lm -lrft

Note that the first option is the prefered option as it offers better throughput and latency. The NNG option is
provided here for compatibility option with legacy applications that want to provide fault tolerance with the RFT.
NNG compatibility will be removed in future versions, and programmers are encouraged to use the SI95 library as
illustrated in the prefered linking option.

Logger levels

The RFT can be compiled with different levels of log messages. By default, log messages are redirected to the default
standard error file descriptor (stderr), which is usually shown in the console terminal.
Different logger levels can be configured in the compilation process by just changing the LOGGER_LEVEL variable
in the Makefile. The following logger levels are supported by the RFT library:

	LOGGER_NONE
	LOGGER_FATAL
	LOGGER_ERROR
	LOGGER_WARN
	LOGGER_INFO
	LOGGER_DEBUG
	LOGGER_TRACE

Each logger level adds more information shown in the standard error respectively from LOGGER_NONE to LOGGER_TRACE.
Basically, this changes the compilation flag -DLOGGER_LEVEL=LOGGER_??? passed to the C compiler.
The default logger level is LOGGER_INFO, and it should be changed to LOGGER_ERROR in production systems since it
produces a lot of messages that may impose significant overheard and degrade the performance of the RFT library.
