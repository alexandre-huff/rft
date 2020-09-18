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
	Mnemonic:	mock_rft_private.cpp
	Abstract:	Implements mock features for the RFT private main module

	Date:		12 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
	#include "rft_private.h"
}


index_t get_full_replicated_log_index( ) {
	return (index_t)mock().actualCall(__func__)
		.returnUnsignedLongIntValueOrDefault( 0 );
}
