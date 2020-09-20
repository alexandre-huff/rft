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
	Mnemonic:	test_snapshot.cpp
	Abstract:	Tests the RFT snashot module

	Date:		18 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"


extern "C" {
	#include <unistd.h>
	#include <sysexits.h>
	#include "snapshot.h"
	#include "stubs/stub_logger.h"
	#include "stubs/stub_pthread.h"
	#include "../../src/static/hashtable.c"
}

size_t take_xapp_snapshot_cb( char **contexts, int nctx, unsigned int *items, unsigned char **data ) {
	return (size_t)mock().actualCall(__func__)
		.withPointerParameter( "contexts", contexts )
		.withIntParameter( "nctx", nctx )
		.withOutputParameter( "items", items )
		.withOutputParameter( "data", data )
		.returnUnsignedLongIntValueOrDefault( 0 );
}

TEST_GROUP( TestSnapshot ) {
	hashtable_t *ctxtable;

	void setup() {
		ctxtable = hashtable_new( STRING_KEY, 27 );
		*get_in_progress() = 0;
	}

	void teardown() {
		// mock().clear();
		hashtable_free( ctxtable );
	}
};

/*
	tests if it allows taking a snapshot while another is in progress
*/
TEST( TestSnapshot, TakeXAppSnapshotInProgress ) {
	*get_in_progress() = 1;	// setting snapshot state to "in progress"
	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );	// should return silently
	CHECK( *get_in_progress() == 1 );	// should still be "in progress"
}

/*
	tests if it returns if there is no snapshot callback
*/
TEST( TestSnapshot, TakeXAppSnapshotNullCallBack ) {
	take_xapp_snapshot( ctxtable, NULL );	// should return silently
	CHECK( *get_in_progress() == 0 );	// should still be "in progress"
}

/*
	tries to get a snapshot that have not been taken
*/
TEST( TestSnapshot, GetXAppSnapshotNotTaken ) {
	snapshot_t *s = get_xapp_snapshot( );
	CHECK_FALSE( s );
}

/*
	test the primary context handler, which is in charge of
	getting all the primary contexts of a given xApp that
	will be snapshotted
*/
TEST_GROUP( TestContexHandler ) {
	hashtable_t *ctxtable;
	int primary = 1;
	int backup = 0;
	primary_ctx_t *pctxs = get_primary_ctxs( );

	void setup() {
		ctxtable = hashtable_new( STRING_KEY, 27 );
		pctxs->len = 0;
		pctxs->size = 1;
		pctxs->contexts = (char **) malloc( pctxs->size * sizeof(char *) );
		CHECK_TRUE( pctxs->contexts );
	}

	void teardown() {
		// mock().clear();
		hashtable_free( ctxtable );
		for( int i = 0; i < pctxs->len; i++ ) {
			free( pctxs->contexts[i] );	// we have to free all added contexts
		}
		free( pctxs->contexts );
	}
};

/*
	tests if primary ctx is added to be snapshotted
	tests if backup ctx is not added to be snapshotted
*/
TEST( TestContexHandler, PrimaryCtxHandlerKeepSize ) {
	hashtable_insert( ctxtable, "ctx1", &primary );
	hashtable_insert( ctxtable, "ctx2", &backup );
	hashtable_foreach_key( ctxtable, primary_ctx_handler );
	UNSIGNED_LONGS_EQUAL( 1, pctxs->len );	// should add primary context
	UNSIGNED_LONGS_EQUAL( 1, pctxs->size );	// should not change the size
	STRCMP_EQUAL( "ctx1", pctxs->contexts[0] );

	hashtable_delete( ctxtable, "ctx1" );
	hashtable_delete( ctxtable, "ctx2" );
}

/*
	tests if primary ctx is added to be snapshotted
	tests if backup ctx is not added to be snapshotted
	and tests if the pctxs double its size when it get full
*/
TEST( TestContexHandler, PrimaryCtxHandlerDoubleSize ) {
	hashtable_insert( ctxtable, "ctx1", &backup );
	hashtable_insert( ctxtable, "ctx2", &primary );
	hashtable_insert( ctxtable, "ctx3", &backup );
	hashtable_insert( ctxtable, "ctx4", &primary );
	hashtable_foreach_key( ctxtable, primary_ctx_handler );
	UNSIGNED_LONGS_EQUAL( 2, pctxs->len );	// should add only ctx2 and ctx4
	UNSIGNED_LONGS_EQUAL( 2, pctxs->size );	// should double the size ( 1 -> 2)
	STRCMP_EQUAL( "ctx2", pctxs->contexts[0] );

	hashtable_delete( ctxtable, "ctx1" );
	hashtable_delete( ctxtable, "ctx2" );
	hashtable_delete( ctxtable, "ctx3" );
	hashtable_delete( ctxtable, "ctx4" );
}

TEST_GROUP( TestChildSnapshot ) {
	hashtable_t *ctxtable;

	void setup() {
		ctxtable = hashtable_new( STRING_KEY, 27 );
		*get_in_progress() = 0;
	}

	void teardown() {
		mock().clear();
		hashtable_free( ctxtable );
	}
};

/*
	tests when snapshot data is smaller than the kernel's max pipe size
	also tests if "in progress" is set after while taking snapshot
*/
TEST( TestChildSnapshot, TakeXAppSnapshot ) {
	int items = 1;
	int size = 32;
	unsigned char *data = (unsigned char *) malloc( size );
	CHECK_TRUE( data );

	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 0 );	// child process
	mock()
		.expectOneCall( "unlock_server_log" );
	mock()
		.expectOneCall( "take_xapp_snapshot_cb" )
		// .withPointerParameter( "contexts", NULL )
		// .withIntParameter( "nctx", 0 )
		.withOutputParameterReturning( "items", &items, sizeof(int) )
		.withOutputParameterReturning( "data", &data, sizeof(data) ) // this is a pointer of pointer, thus 8 bytes (not 32)
		.ignoreOtherParameters()
		.andReturnValue( size );
	mock()
		.expectOneCall( "get_server_last_log_index" )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(ctx_metabuf_t) );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( size );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EXIT_SUCCESS );

	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );

	mock().checkExpectations();

	CHECK( *get_in_progress() == 1 );
	ctx_metabuf_t *metabuf = get_ctx_metabuf( );
	LONGS_EQUAL( size, metabuf->dlen );
	LONGS_EQUAL( items, metabuf->items );
	LONGS_EQUAL( 1, metabuf->last_log_index );
}

/*
	tests when snapshot data is greater than the kernel's max pipe size
	also tests if "in progress" is set after while taking snapshot
*/
TEST( TestChildSnapshot, TakeXAppSnapshotMaxPipeSize ) {
	int items = 1;
	int pdata[2];
	CHECK( pipe( pdata ) == 0 );	// opening a pipe just to get its max size
	long max_pipe_buf = fpathconf( pdata[1], _PC_PIPE_BUF );
	long size = max_pipe_buf + 1;	// just greater than pipe size
	close( pdata[0] );
	close( pdata[1] );

	unsigned char *data = (unsigned char *) malloc( size );
	CHECK_TRUE( data );

	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "unlock_server_log" );
	mock()
		.expectOneCall( "take_xapp_snapshot_cb" )
		.withOutputParameterReturning( "items", &items, sizeof(int) )
		.withOutputParameterReturning( "data", &data, sizeof(data) ) // this is a pointer of pointer, thus 8 bytes (not size)
		.ignoreOtherParameters( )
		.andReturnValue( size );
	mock()
		.expectOneCall( "get_server_last_log_index" )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(ctx_metabuf_t) );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)max_pipe_buf );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)(size - max_pipe_buf) );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EXIT_SUCCESS );

	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );

	mock().checkExpectations();

	CHECK( *get_in_progress() == 1 );
	ctx_metabuf_t *metabuf = get_ctx_metabuf( );
	LONGS_EQUAL( size, metabuf->dlen );
	LONGS_EQUAL( items, metabuf->items );
	LONGS_EQUAL( 1, metabuf->last_log_index );
}

/*
	tests pipe write error on xapp snapshot meta-data
*/
TEST( TestChildSnapshot, TakeXAppSnapshotMetaPipeError ) {
	int items = 1;
	int size = 32;

	unsigned char *data = (unsigned char *) malloc( size );
	CHECK_TRUE( data );

	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "unlock_server_log" );
	mock()
		.expectOneCall( "take_xapp_snapshot_cb" )
		.withOutputParameterReturning( "items", &items, sizeof(int) )
		.withOutputParameterReturning( "data", &data, sizeof(data) ) // this is a pointer of pointer, thus 8 bytes (not size)
		.ignoreOtherParameters( )
		.andReturnValue( size );
	mock()
		.expectOneCall( "get_server_last_log_index" )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EX_IOERR );

	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );

	mock().checkExpectations();

	CHECK( *get_in_progress() == 1 );
}

/*
	tests pipe write error on xapp snapshot data
*/
TEST( TestChildSnapshot, TakeXAppSnapshotDataPipeError ) {
	int items = 1;
	int size = 32;

	unsigned char *data = (unsigned char *) malloc( size );
	CHECK_TRUE( data );

	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "unlock_server_log" );
	mock()
		.expectOneCall( "take_xapp_snapshot_cb" )
		.withOutputParameterReturning( "items", &items, sizeof(int) )
		.withOutputParameterReturning( "data", &data, sizeof(data) ) // this is a pointer of pointer, thus 8 bytes (not size)
		.ignoreOtherParameters( )
		.andReturnValue( size );
	mock()
		.expectOneCall( "get_server_last_log_index" )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(ctx_metabuf_t) );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EX_IOERR );

	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );

	mock().checkExpectations();

	CHECK( *get_in_progress() == 1 );
}

TEST_GROUP( TestParentSnapshot ) {
	hashtable_t *ctxtable;
	ctx_metabuf_t meta;
	unsigned char *data;
	snapshot_t *ctx_snapshot = get_ctx_snapshot( );
	const index_t last_log_index = 1;
	const size_t dlen = 32;
	const unsigned int items = 2;

	void setup() {
		ctxtable = hashtable_new( STRING_KEY, 27 );
		meta.last_log_index = last_log_index;
		meta.dlen = dlen;
		meta.items = items;
		ctx_snapshot->dlen = 0;
		ctx_snapshot->items = 0;
		ctx_snapshot->last_log_index = 0;
		data = (unsigned char *) malloc( meta.dlen );
		CHECK_TRUE( data );
		memset( data, 0, meta.dlen );
		*get_in_progress() = 1;
	}

	void teardown() {
		mock().clear();

		hashtable_free( ctxtable );
		free( data );
		free( ctx_snapshot->data );
		ctx_snapshot->data = NULL;
	}
};

/*
	tests if there is a branch to start the parent thread to receive snapshot data
*/
TEST( TestParentSnapshot, TakeXAppSnapshot ) {
	*get_in_progress() = 0;
	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 1 );	// parent process is > 0

	// should mock pthread call as well
	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );
	CHECK( *get_in_progress() == 1 );	// should still be "in progress"
}

/*
	Tests the normal flow of the parent thread
*/
TEST( TestParentSnapshot, ParentThread ) {
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(meta) );
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", data, meta.dlen )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)meta.dlen );
	mock()
		.expectOneCall( "compact_server_log" );
	mock()
		.expectOneCall( "wait" )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_thread( );
	mock().checkExpectations();

	STRNCMP_EQUAL( (const char *)data, (const char *)ctx_snapshot->data, meta.dlen );
	UNSIGNED_LONGS_EQUAL( meta.last_log_index, ctx_snapshot->last_log_index );
	UNSIGNED_LONGS_EQUAL( meta.dlen, ctx_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( meta.items, ctx_snapshot->items );
	LONGS_EQUAL( 0, *get_in_progress() );
}

/*
	Tests the get xapp snapshot data with snapshot already taken
	We need to "take" the snapshot first
*/
TEST( TestParentSnapshot, GetXAppSnapshot ) {
	// taking snapshot from here
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(meta) );
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", data, meta.dlen )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)meta.dlen );
	mock()
		.expectOneCall( "compact_server_log" );
	mock()
		.expectOneCall( "wait" )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_thread( );
	mock().checkExpectations();
	// taking snapshot up to here

	snapshot_t *s = get_xapp_snapshot( );

	STRNCMP_EQUAL( (const char *)data, (const char *)s->data, meta.dlen );
	UNSIGNED_LONGS_EQUAL( meta.last_log_index, s->last_log_index );
	UNSIGNED_LONGS_EQUAL( meta.dlen, s->dlen );
	UNSIGNED_LONGS_EQUAL( meta.items, s->items );
	LONGS_EQUAL( 0, *get_in_progress() );

	free( s->data );
}

/*
	Tests if pipe reads all bytes when snapshot size exceeds max pipe size
*/
TEST( TestParentSnapshot, ParentThreadMaxPipeSize ) {
	int status = EXIT_SUCCESS;
	int pdata[2];
	CHECK( pipe( pdata ) == 0 );	// opening a pipe just to get its max size
	long max_pipe_buf = fpathconf( pdata[1], _PC_PIPE_BUF );
	long size = max_pipe_buf + 1;	// just greater than pipe size
	close( pdata[0] );
	close( pdata[1] );
	meta.dlen = max_pipe_buf + 1;

	unsigned char *read_data = (unsigned char *) malloc( meta.dlen );
	CHECK_TRUE( read_data );

	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(meta) );
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", read_data, max_pipe_buf )	// max pipe size (data)
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)max_pipe_buf );
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", read_data, meta.dlen - max_pipe_buf )	// remaining data
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)(meta.dlen - max_pipe_buf) );
	mock()
		.expectOneCall( "compact_server_log" );
	mock()
		.expectOneCall( "wait" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(int) )
		.andReturnValue( 1 );

	parent_thread( );
	mock().checkExpectations();

	STRNCMP_EQUAL( (const char *)data, (const char *)ctx_snapshot->data, meta.dlen );
	UNSIGNED_LONGS_EQUAL( meta.last_log_index, ctx_snapshot->last_log_index );
	UNSIGNED_LONGS_EQUAL( meta.dlen, ctx_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( meta.items, ctx_snapshot->items );
	LONGS_EQUAL( 0, *get_in_progress() );

	free( read_data );
}

/*
	tests error on reading metadata pipe
	also tests when exit_status is != EXIT_SUCCESS
*/
TEST( TestParentSnapshot, ParentThreadMetaPipeError ) {
	int status = EX_IOERR;
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "wait" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(int) )
		.andReturnValue( 1 );

	parent_thread( );
	mock().checkExpectations();

	CHECK_FALSE( ctx_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, ctx_snapshot->last_log_index );
	UNSIGNED_LONGS_EQUAL( 0, ctx_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, ctx_snapshot->items );
	LONGS_EQUAL( 0, *get_in_progress() );
}

/*
	tests error on reading snapshot data from the pipe
	also tests when wait command fails
*/
TEST( TestParentSnapshot, ParentThreadDataPipeError ) {
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(meta) );
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", data, meta.dlen )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "wait" )
		.ignoreOtherParameters()
		.andReturnValue( 0 );

	parent_thread( );
	mock().checkExpectations();

	CHECK_TRUE( ctx_snapshot->data );	// memory should be allocated
	UNSIGNED_LONGS_EQUAL( 0, ctx_snapshot->last_log_index );
	UNSIGNED_LONGS_EQUAL( 0, ctx_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, ctx_snapshot->items );
	LONGS_EQUAL( 0, *get_in_progress() );
}
