// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2020 AT&T Intellectual Property.
	Copyright (c) 2020 Alexandre Huff.

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

		http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
==================================================================================
*/

/*
	Mnemonic:	mock_syscalls.cpp
	Abstract:	Implements mock features for system calls

	Date:		19 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {

	pid_t __wrap_fork( ) {
		return (pid_t)mock()
			.actualCall( "fork" )
			.returnIntValueOrDefault( 0 );	// means child process
	}

	void __wrap__exit(int __status) {
		mock()
			.actualCall( "_exit" )
			.withIntParameter( "__status", __status );
	}

	pid_t __wrap_wait(int *__stat_loc) {
		return (pid_t)mock()
			.actualCall( "wait" )
			.withOutputParameter( "__stat_loc", __stat_loc )
			.returnIntValueOrDefault( 0 );
	}

	pid_t __wrap_waitpid(pid_t __pid, int *__stat_loc, int __options) {
		return (pid_t)mock()
			.actualCall( "waitpid" )
			.withParameter( "__pid", __pid )
			.withOutputParameter( "__stat_loc", __stat_loc )
			.withParameter( "__options", __options )
			.returnIntValueOrDefault( 0 );
	}

	ssize_t __wrap_write(int __fd, const void *__buf, size_t __n) {
		return (ssize_t)mock()
			.actualCall( "write" )
			.withIntParameter( "__fd", __fd )
			.withConstPointerParameter( "__buf", __buf )
			.withUnsignedLongIntParameter( "__n", __n )
			.returnLongIntValueOrDefault( 0 );
	}

	ssize_t __wrap_read(int __fd, void *__buf, size_t __nbytes) {

		if( mock().hasData( "read_pipe" ) ) {
				/*
					In this case we do not mock the exact behavior of the read system call, instead, we check
					if the read_pipe function passes the correct ptr value to the read system call
				*/
			return (ssize_t)mock()
				.actualCall( "read" )
				.withIntParameter( "__fd", __fd )
				.withPointerParameter( "__buf", __buf )
				.withUnsignedLongIntParameter( "__nbytes", __nbytes )
				.returnLongIntValueOrDefault( 0 );
		}

		/*
			Here the __buf is returned and mocks the exact behavior of the read system call
			It is used by other functions that depends on and call the read_pipe function
		*/
		return (ssize_t)mock()
			.actualCall( "read" )
			.withIntParameter( "__fd", __fd )
			.withOutputParameter( "__buf", __buf )
			.withUnsignedLongIntParameter( "__nbytes", __nbytes )
			.returnLongIntValueOrDefault( 0 );
	}

}

