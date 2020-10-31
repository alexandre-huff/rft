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

int serialize_xapp_snapshot( rmr_mbuf_t **msg, server_id_t *server_id ) {
	return mock().actualCall(__func__)
		.withOutputParameter( "msg", msg )
		.withPointerParameter( "server_id", server_id )
		.returnIntValueOrDefault( 0 );
}

int serialize_raft_snapshot( rmr_mbuf_t **msg ) {
	return mock().actualCall(__func__)
		.withOutputParameter( "msg", msg )
		.returnIntValueOrDefault( 0 );
}

index_t get_raft_snapshot_last_index( ) {
	return (index_t)mock().actualCall(__func__)
		.returnUnsignedLongIntValueOrDefault( 0 );
}
index_t get_raft_snapshot_last_term( ) {
	return (index_t)mock().actualCall(__func__)
		.returnUnsignedLongIntValueOrDefault( 0 );
}

void take_raft_snapshot( ) {
	mock().actualCall(__func__);
}

int install_raft_snapshot( raft_snapshot_t *snapshot ) {
	return mock().actualCall(__func__)
		.withPointerParameter( "snapshot", snapshot )
		.returnIntValueOrDefault( 0 );
}
