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
	Mnemonic:	mock_snapshot.cpp
	Abstract:	Implements mock features for the RFT snapshot module

	Date:		11 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
	#include "snapshot.h"
	#include <stddef.h>
}

void take_xapp_snapshot( hashtable_t *ctxtable, take_snapshot_cb_t take_snapshot_cb ) {
	mock().actualCall(__func__)
		.withPointerParameter("ctxtable", ctxtable)
		.withPointerParameter("take_snapshot_cb", (take_snapshot_cb_t *) take_snapshot_cb);
}

snapshot_t *get_xapp_snapshot( ) {
	return (snapshot_t *)mock().actualCall(__func__)
		.returnPointerValueOrDefault((void *) NULL);
}
