// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2020 AT&T Intellectual Property.

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
	Mnemonic:	fake_string.cpp
	Abstract:	Implements fake features for the system's string module

	Date:		2 September 2020
	Author:		Alexandre Huff
*/

#include <string.h>
#include <stdio.h>

/*
	Returns a pointer a new string which is a clone of str.

	At most max_len bytes are cloned and an extra terminating
	null byte '\0' is added to the cloned str.

	This function is meant to be used as replacement of the
	strndup since it is *not* a Standard C function	but a
	POSIX function.
	This function avoids that the memory leak detector of
	the testing framework (CppUTest) complains about memory likage.

	This function also avoids the requirement of disabling the
	memory leak detector during the tests since it reports false
	positive when using strdup and strndup. The following are
	the functions do disable/enable the memory leak detector
	which are no longer necessary:
	- MemoryLeakWarningPlugin::turnOffNewDeleteOverloads();
	and
	- MemoryLeakWarningPlugin::turnOnNewDeleteOverloads();
*/
char *strndup( const char *str, size_t max_len ) {
    char *s;
	size_t len;

	if( str != NULL && max_len ) {
		len = strlen( str ) + 1;
		if( len > max_len )
			len = max_len + 1;
		s = (char*) cpputest_malloc_location( len, __FILE__, __LINE__ );
		// s = (char*) malloc( len );
		if( s != NULL ) {
			memcpy( s, str, len - 1 );
			s[len-1] = '\0';
			return s;
		}
	}

	return NULL;
}
