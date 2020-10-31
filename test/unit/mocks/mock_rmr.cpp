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
	Mnemonic:	mock_rmr.cpp
	Abstract:	Implements mock features of the RMR library for the RFT

	Date:		22 October 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
	#include <rmr/rmr.h>
}

extern int rmr_payload_size( rmr_mbuf_t* msg ) {
	return mock().actualCall(__func__)
		.withPointerParameter( "msg", msg )
		.returnIntValueOrDefault( 0 );
}

extern rmr_mbuf_t* rmr_realloc_payload( rmr_mbuf_t* old_msg, int new_len, int copy, int clone ) {
	return (rmr_mbuf_t *)mock().actualCall(__func__)
		.withPointerParameter( "old_msg", old_msg )
		.withParameter( "new_len", new_len )
		.withParameter( "copy", copy )
		.withParameter( "clone", clone )
		.returnPointerValueOrDefault( (rmr_mbuf_t *) NULL );
}

extern rmr_mbuf_t* rmr_send_msg( void* vctx, rmr_mbuf_t* msg ) {
	return (rmr_mbuf_t *)mock().actualCall(__func__)
		.withParameter( "vctx", vctx )
		.withPointerParameter( "msg", msg )
		.returnPointerValueOrDefault( (rmr_mbuf_t *) NULL );
}

extern rmr_mbuf_t* rmr_rts_msg( void* vctx, rmr_mbuf_t* msg ) {
	return (rmr_mbuf_t *)mock().actualCall(__func__)
		.withParameter( "vctx", vctx )
		.withPointerParameter( "msg", msg )
		.returnPointerValueOrDefault( (rmr_mbuf_t *) NULL );
}

extern rmr_mbuf_t* rmr_wh_send_msg( void* vctx, rmr_whid_t whid, rmr_mbuf_t* msg ) {
	return (rmr_mbuf_t *)mock().actualCall(__func__)
		.withParameter( "vctx", vctx )
		.withParameter( "whid", whid )
		.withPointerParameter( "msg", msg )
		.returnPointerValueOrDefault( (rmr_mbuf_t *) NULL );
}

extern rmr_mbuf_t* rmr_alloc_msg( void* vctx, int size ) {
	return (rmr_mbuf_t *)mock().actualCall(__func__)
		.withParameter( "vctx", vctx )
		.withParameter( "size", size )
		.returnPointerValueOrDefault( (rmr_mbuf_t *) NULL );
}

extern rmr_whid_t rmr_wh_open( void* vctx, char const* target ) {
	return (rmr_whid_t)mock().actualCall(__func__)
		.withParameter( "vctx", vctx )
		.withParameter( "target", target )
		.returnIntValueOrDefault( 0 );
}

extern void rmr_wh_close( void* vctx, int whid ) {
	mock().actualCall(__func__)
		.withParameter( "vctx", vctx )
		.withParameter( "whid", whid );
}

extern void rmr_free_msg( rmr_mbuf_t* mbuf ) {
	mock().actualCall(__func__)
		.withPointerParameter( "mbuf", mbuf);
}

extern unsigned char* rmr_get_src( rmr_mbuf_t* mbuf, unsigned char* dest ) {
	return (unsigned char *)mock().actualCall(__func__)
		.withPointerParameter( "mbuf", mbuf )
		.withParameter( "dest", dest )
		.returnStringValueOrDefault( "\0" );
}
