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
	Mnemonic:	mock_log.cpp
	Abstract:	Implements mock features for the RFT log module

	Date:		18 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
	#include "log.h"
}


index_t get_server_last_log_index( ) {
	return (index_t)mock().actualCall(__func__)
		.returnUnsignedLongIntValueOrDefault( 0 );
}

void compact_server_log( ) {
	mock().actualCall(__func__);
}

void unlock_server_log( ) {
	mock().actualCall(__func__);
}
