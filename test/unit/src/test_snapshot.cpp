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
	Mnemonic:	test_snapshot.cpp
	Abstract:	Tests the RFT snashot module

	Date:		18 September 2020
	Author:		Alexandre Huff

	Updated:	28 October 2020
				Implemented tests of Raft snapshotting
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"


extern "C" {
	#include <unistd.h>
	#include <sysexits.h>
	#include <signal.h>
	#include <rmr/rmr.h>
	#include "snapshot.h"
	#include "mtl.h"
	#include "stubs/stub_logger.h"
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

TEST_GROUP( TestPipe ) {
	unsigned char *buffer = NULL;
	size_t blen = PIPE_BUF + 64;
	int fd = 5;		// general file descriptor

	void setup() {
		buffer = (unsigned char *) malloc( blen );
		if( buffer == NULL )
			FAIL( "unable to allocate a buffer for testing pipes" );
	}

	void teardown() {
		mock().clear();
		if( buffer ) {
			free( buffer );
			buffer = NULL;
		}

	}
};

/*
	Tests the pipe reading a snapshot smaller than PIPE_BUF
*/
TEST( TestPipe, ReadPipeSmallerSnapshot ) {
	mock().setData( "read_pipe", 1 );
	mock()
		.expectOneCall( "read" )
		.withParameter( "__fd", fd )
		.withPointerParameter( "__buf", buffer )
		.withParameter( "__nbytes", PIPE_BUF - 1 )
		.ignoreOtherParameters()
		.andReturnValue( PIPE_BUF - 1 );

	int success = read_pipe( fd, buffer, PIPE_BUF - 1 );
	mock().checkExpectations();

	CHECK_TRUE( success );
}

/*
	Tests the pipe reading a snapshot larger than PIPE_BUF
*/
TEST( TestPipe, ReadPipeLargerSnapshot ) {
	mock().setData( "read_pipe", 1 );
	mock()
		.expectOneCall( "read" )
		.withParameter( "__fd", fd )
		.withPointerParameter( "__buf", buffer )
		.withParameter( "__nbytes", PIPE_BUF )
		.ignoreOtherParameters()
		.andReturnValue( PIPE_BUF );
	mock()
		.expectOneCall( "read" )
		.withParameter( "__fd", fd )
		.withPointerParameter( "__buf", buffer + PIPE_BUF )
		.withParameter( "__nbytes", 1 )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	int success = read_pipe( fd, buffer, PIPE_BUF + 1 );
	mock().checkExpectations();

	CHECK_TRUE( success );
}

/*
	Tests the pipe reading a snapshot with length equals to PIPE_BUF
*/
TEST( TestPipe, ReadPipeEqualsSnapshot ) {
	mock().setData( "read_pipe", 1 );
	mock()
		.expectOneCall( "read" )
		.withParameter( "__fd", fd )
		.withPointerParameter( "__buf", buffer )
		.withParameter( "__nbytes", PIPE_BUF )
		.andReturnValue( PIPE_BUF );

	int success = read_pipe( fd, buffer, PIPE_BUF );
	mock().checkExpectations();

	CHECK_TRUE( success );
}

/*
	Tests a unsucessful pipe reading
*/
TEST( TestPipe, ReadPipeError ) {
	mock().setData( "read_pipe", 1 );
	mock()
		.expectOneCall( "read" )
		.withParameter( "__fd", fd )
		.withPointerParameter( "__buf", buffer )
		.withParameter( "__nbytes", 10 )
		.andReturnValue( -1 );

	int success = read_pipe( fd, buffer, 10 );
	mock().checkExpectations();

	CHECK_FALSE( success );
}

/*
	Tests the pipe writing a snapshot smaller than PIPE_BUF
*/
TEST( TestPipe, WritePipeSmallerSnapshot ) {
	mock()
		.expectOneCall( "write" )
		.withParameter( "__fd", fd )
		.withConstPointerParameter( "__buf", buffer )
		.withParameter( "__n", PIPE_BUF - 1 )
		.andReturnValue( PIPE_BUF - 1 );

	int success = write_pipe( fd, buffer, PIPE_BUF - 1 );
	mock().checkExpectations();

	CHECK_TRUE( success );
}

/*
	Tests the pipe writing a snapshot larger than PIPE_BUF
*/
TEST( TestPipe, WritePipeLargerSnapshot ) {
	mock()
		.expectOneCall( "write" )
		.withParameter( "__fd", fd )
		.withConstPointerParameter( "__buf", buffer )
		.withParameter( "__n", PIPE_BUF )
		.andReturnValue( PIPE_BUF );
	mock()
		.expectOneCall( "write" )
		.withParameter( "__fd", fd )
		.withConstPointerParameter( "__buf", buffer + PIPE_BUF )
		.withParameter( "__n", 1 )
		.andReturnValue( 1 );

	int success = write_pipe( fd, buffer, PIPE_BUF + 1 );
	mock().checkExpectations();

	CHECK_TRUE( success );
}

/*
	Tests the pipe writing a snapshot with length equals to PIPE_BUF
*/
TEST( TestPipe, WritePipeEqualsSnapshot ) {
	mock()
		.expectOneCall( "write" )
		.withParameter( "__fd", fd )
		.withConstPointerParameter( "__buf", buffer )
		.withParameter( "__n", PIPE_BUF )
		.andReturnValue( PIPE_BUF );

	int success = write_pipe( fd, buffer, PIPE_BUF );
	mock().checkExpectations();

	CHECK_TRUE( success );
}

/*
	Tests a unsucessful pipe writing
*/
TEST( TestPipe, WritePipeError ) {
	mock()
		.expectOneCall( "write" )
		.withParameter( "__fd", fd )
		.withConstPointerParameter( "__buf", buffer )
		.withParameter( "__n", 10 )
		.andReturnValue( -1 );

	int success = write_pipe( fd, buffer, 10 );
	mock().checkExpectations();

	CHECK_FALSE( success );
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
		ctxtable = hashtable_new( STRING_KEY, 31 );
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

/*
	Tests the take_xapp_snapshot function, which starts a new snapshot and
	also runs as the child process
*/
TEST_GROUP( TestTakeXAppSnapshot ) {
	hashtable_t *ctxtable;

	void setup() {
		ctxtable = hashtable_new( STRING_KEY, 31 );
		*get_xapp_in_progress() = 0;
	}

	void teardown() {
		mock().clear();
		hashtable_free( ctxtable );
	}
};

/*
	tests if it returns if there is no snapshot callback
*/
TEST( TestTakeXAppSnapshot, TakeXAppSnapshotNullCallBack ) {
	take_xapp_snapshot( ctxtable, NULL );	// should return silently
	CHECK( *get_xapp_in_progress() == 0 );	// should still be in the same progress state
}

/*
	tests if it allows taking a snapshot while another is in progress
*/
TEST( TestTakeXAppSnapshot, TakeXAppSnapshotInProgress ) {
	*get_xapp_in_progress() = 1;	// setting snapshot state to "in progress"
	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );	// should return silently
	CHECK( *get_xapp_in_progress() == 1 );	// should still be "in progress"
}

/*
	tests if there is a branch to start the xApp parent thread to receive snapshot data
*/
TEST( TestTakeXAppSnapshot, TakeXAppSnapshotStartParentThread ) {
	*get_xapp_in_progress() = 0;
	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 1 );	// parent process is > 0
	mock()
		.expectOneCall( "pthread_create" )
		.withPointerParameter( "__start_routine", (void *)&parent_xapp_thread )
		.ignoreOtherParameters()
		.andReturnValue( 0 );

	// should mock pthread call as well
	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );
	mock().checkExpectations();
	CHECK( *get_xapp_in_progress() == 1 );	// should still be "in progress"
}

/*
	tests the regular flow of the take_xapp_snapshot function
	also tests if "in progress" is set while taking snapshot
*/
TEST( TestTakeXAppSnapshot, TakeXAppSnapshot ) {
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
		.withOutputParameterReturning( "items", &items, sizeof(items) )
		.withOutputParameterReturning( "data", &data, sizeof(data) ) // this is a pointer of pointer, thus 8 bytes (not 32)
		.ignoreOtherParameters()
		.andReturnValue( size );
	mock()
		.expectOneCall( "get_server_last_log_index" )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(pipe_metabuf_t) );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( size );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EXIT_SUCCESS );

	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );

	mock().checkExpectations();

	CHECK( *get_xapp_in_progress() == 1 );
	pipe_metabuf_t *metabuf = get_xapp_metabuf( );
	LONGS_EQUAL( size, metabuf->dlen );
	LONGS_EQUAL( items, metabuf->items );
	LONGS_EQUAL( 1, metabuf->last_index );
}

/*
	tests error on writing the snapshot metadata to the pipe
*/
TEST( TestTakeXAppSnapshot, TakeXAppSnapshotMetaPipeError ) {
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

	CHECK( *get_xapp_in_progress() == 1 );
}

/*
	tests error on writing the snapshot serialized data to the pipe
*/
TEST( TestTakeXAppSnapshot, TakeXAppSnapshotDataPipeError ) {
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
		.andReturnValue( (ssize_t)sizeof(pipe_metabuf_t) );
	mock()
		.expectOneCall( "write" )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EX_IOERR );

	take_xapp_snapshot( ctxtable, &take_xapp_snapshot_cb );

	mock().checkExpectations();

	CHECK( *get_xapp_in_progress() == 1 );
}

TEST_GROUP( TestXAppParentSnapshot ) {
	pipe_metabuf_t meta;
	unsigned char *data;
	xapp_snapshot_t *xapp_snapshot = get_xapp_snapshot( );
	const index_t last_log_index = 1;
	const size_t dlen = 32;
	const unsigned int items = 2;

	void setup() {
		meta.last_index = last_log_index;
		meta.dlen = dlen;
		meta.items = items;
		xapp_snapshot->dlen = 0;
		xapp_snapshot->items = 0;
		xapp_snapshot->last_index = 0;
		data = (unsigned char *) malloc( meta.dlen );
		if( !data )
			FAIL( "unable to allocate memory for xApp snapshot data" );
		memset( data, 0, meta.dlen );

		*get_xapp_in_progress() = 1;
	}

	void teardown() {
		mock().clear();
		free( data );
		free( xapp_snapshot->data );
		xapp_snapshot->data = NULL;
	}
};

/*
	Tests the normal flow of the parent thread
*/
TEST( TestXAppParentSnapshot, XAppParentThread ) {
	// we are simulating the child _exit(EXIT_SUCCESS) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EXIT_SUCCESS, 0 );

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
		.expectOneCall( "compact_server_log" )
		.withParameter( "to_index", meta.last_index );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_xapp_thread( );
	mock().checkExpectations();

	MEMCMP_EQUAL( data, xapp_snapshot->data, meta.dlen );
	UNSIGNED_LONGS_EQUAL( meta.last_index, xapp_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( meta.dlen, xapp_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( meta.items, xapp_snapshot->items );
	LONGS_EQUAL( 0, *get_xapp_in_progress() );
}

/*
	tests error on reading the metadata from the pipe
*/
TEST( TestXAppParentSnapshot, XAppParentThreadMetaPipeError ) {
	// we are simulating the child _exit(EXIT_SUCCESS) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EXIT_SUCCESS, 0 );

	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_xapp_thread( );
	mock().checkExpectations();

	CHECK_FALSE( xapp_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->items );
	LONGS_EQUAL( 0, *get_xapp_in_progress() );
}

/*
	tests error on reading snapshot data from the pipe
*/
TEST( TestXAppParentSnapshot, XAppParentThreadDataPipeError ) {
	// we are simulating the child _exit(EXIT_SUCCESS) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EXIT_SUCCESS, 0 );

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
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_xapp_thread( );
	mock().checkExpectations();

	CHECK_TRUE( xapp_snapshot->data );	// memory should be allocated
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->items );
	LONGS_EQUAL( 0, *get_xapp_in_progress() );
}

/*
	Tests when the waitpid system call fails
*/
TEST( TestXAppParentSnapshot, XAppParentThreadWaitpidError ) {
	// we fail the first read here to avoid writing unneeded testing code
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.ignoreOtherParameters()
		.andReturnValue( -1 );

	parent_xapp_thread( );
	mock().checkExpectations();

	CHECK_FALSE( xapp_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->items );
	LONGS_EQUAL( 0, *get_xapp_in_progress() );
}

/*
	Tests when the child exits with an error (exit_status != EXIT_SUCCESS)
*/
TEST( TestXAppParentSnapshot, XAppParentThreadChildExitError ) {
	// we are simulating the child _exit(EX_IOERR) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EX_IOERR, 0 );

	// we fail the first read here to avoid writing unneeded testing code
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_xapp_thread( );
	mock().checkExpectations();

	CHECK_FALSE( xapp_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->items );
	LONGS_EQUAL( 0, *get_xapp_in_progress() );
}

/*
	Tests when the child exits with a signal error
*/
TEST( TestXAppParentSnapshot, XAppParentThreadChildExitSignal ) {
	// we are simulating the child received a SIGTERM signal (could be any signal)
	int status = __W_EXITCODE( 0, SIGTERM );	// a signal does not have a return status code, thus we use 0

	// we fail the first read here to avoid writing unneeded testing code
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_xapp_thread( );
	mock().checkExpectations();

	CHECK_FALSE( xapp_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, xapp_snapshot->items );
	LONGS_EQUAL( 0, *get_xapp_in_progress() );
}

TEST_GROUP( TestSerializeXAppSnapshot ) {
	xapp_snapshot_t *xapp_snapshot = get_xapp_snapshot( );
	const index_t last_log_index = 1;
	const size_t dlen = 32;
	const unsigned int items = 2;
	server_id_t server_id;
	rmr_mbuf_t *msg;

	void setup() {
		xapp_snapshot->dlen = dlen;
		xapp_snapshot->items = items;
		xapp_snapshot->last_index = last_log_index;
		xapp_snapshot->data = (unsigned char *) malloc( dlen );
		if( !xapp_snapshot->data )
			FAIL( "unable to allocate memory for xApp snapshot data" );
		memset( xapp_snapshot->data, 0, xapp_snapshot->dlen );

		snprintf( server_id, sizeof(server_id_t), "xapp1" );
		snprintf( (char *)xapp_snapshot->data, dlen, "testing snapshot bytes" );

		msg = (rmr_mbuf_t *) malloc( sizeof(rmr_mbuf_t) );
		if( msg ) {
			msg->payload = (unsigned char *) malloc( RMR_MAX_RCV_BYTES );
			if( !msg->payload )
				FAIL( "unable to allocate payload to the message buffer" );
		} else {
			FAIL( "unable to allocate an RMR message buffer" );
		}
	}

	void teardown() {
		mock().clear();
		free( xapp_snapshot->data );
		xapp_snapshot->data = NULL;
		free( msg->payload );
		free( msg );
	}
};

/*
	Tries to serialize an xApp snapshot that has not been taken
*/
TEST( TestSerializeXAppSnapshot, SerializeXAppSnapshotNotTaken ) {
	unsigned char *tmp = xapp_snapshot->data;
	xapp_snapshot->data = NULL;

	int len = serialize_xapp_snapshot( &msg, &server_id );
	CHECK_FALSE( len );

	xapp_snapshot->data = tmp;
}


/*
	Tests the serialization of the xApp snapshot into the RMR message buffer
*/
TEST( TestSerializeXAppSnapshot, SerializeXAppSnapshot ) {
	mock()
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( RMR_MAX_RCV_BYTES );

	int len = serialize_xapp_snapshot( &msg, &server_id );
	mock().checkExpectations();

	xapp_snapshot_hdr_t *payload = (xapp_snapshot_hdr_t *) msg->payload;

	MEMCMP_EQUAL( xapp_snapshot->data, XAPP_SNAPSHOT_PAYLOAD_ADDR( msg->payload ), dlen );
	UNSIGNED_LONGS_EQUAL( HTONLL( xapp_snapshot->last_index ), payload->last_index );
	UNSIGNED_LONGS_EQUAL( HTONLL( xapp_snapshot->dlen ), payload->dlen );
	UNSIGNED_LONGS_EQUAL( htonl( xapp_snapshot->items ), payload->items );
	STRCMP_EQUAL( server_id, payload->server_id );
}

/*
	Tests if the RMR payload is reallocated if the snapshot size is greater than the RMR message buffer can hold
*/
TEST( TestSerializeXAppSnapshot, SerializeXAppSnapshotReallocPayload ) {
	mock()
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "rmr_realloc_payload" )
		.withPointerParameter( "old_msg", msg )
		.withParameter( "new_len", XAPP_SNAPSHOT_HDR_SIZE + dlen )
		.withParameter( "copy", 0 )
		.withParameter( "clone", 0 )
		.andReturnValue( msg );

	int len = serialize_xapp_snapshot( &msg, &server_id );
	mock().checkExpectations();

	xapp_snapshot_hdr_t *payload = (xapp_snapshot_hdr_t *) msg->payload;

	MEMCMP_EQUAL( xapp_snapshot->data, XAPP_SNAPSHOT_PAYLOAD_ADDR( msg->payload ), dlen );
	UNSIGNED_LONGS_EQUAL( HTONLL( xapp_snapshot->last_index ), payload->last_index );
	UNSIGNED_LONGS_EQUAL( HTONLL( xapp_snapshot->dlen ), payload->dlen );
	UNSIGNED_LONGS_EQUAL( htonl( xapp_snapshot->items ), payload->items );
	STRCMP_EQUAL( server_id, payload->server_id );
}

TEST_GROUP( TestSerializeRaftSnapshot ) {
	raft_snapshot_t *raft_snapshot = get_raft_snapshot( );
	const index_t last_log_index = 3;
	const index_t last_log_term = 2;
	const size_t dlen = 128;
	const unsigned int items = 2;
	rmr_mbuf_t *msg;

	void setup() {
		raft_snapshot->dlen = dlen;
		raft_snapshot->items = items;
		raft_snapshot->last_index = last_log_index;
		raft_snapshot->last_term = last_log_term;
		raft_snapshot->data = (unsigned char *) malloc( dlen );
		if( !raft_snapshot->data )
			FAIL( "unable to allocate memory for Raft snapshot data" );
		memset( raft_snapshot->data, 0, raft_snapshot->dlen );

		snprintf( (char *)raft_snapshot->data, dlen, "testing bytes");

		msg = (rmr_mbuf_t *) malloc( sizeof(rmr_mbuf_t) );
		if( msg ) {
			msg->payload = (unsigned char *) malloc( RMR_MAX_RCV_BYTES );
			if( !msg->payload )
				FAIL( "unable to allocate payload to the message buffer" );
		} else {
			FAIL( "unable to allocate an RMR message buffer" );
		}
	}

	void teardown() {
		mock().clear();
		free( raft_snapshot->data );
		raft_snapshot->data = NULL;
		free( msg->payload );
		free( msg );
	}
};

/*
	Tests the returned value from the last log index in snapshot
*/
TEST( TestSerializeRaftSnapshot, GetSnapshotLastIndex ) {
	UNSIGNED_LONGS_EQUAL( last_log_index, get_raft_snapshot_last_index() );
}

/*
	Tests the returned value from the last term in snapshot
*/
TEST( TestSerializeRaftSnapshot, GetSnapshotLastTerm ) {
	UNSIGNED_LONGS_EQUAL( last_log_term, get_raft_snapshot_last_term() );
}

/*
	Tries to serialize a Raft snapshot that has not been taken
*/
TEST( TestSerializeRaftSnapshot, SerializeRaftSnapshotNotTaken ) {
	unsigned char *tmp = raft_snapshot->data;
	raft_snapshot->data = NULL;

	int len = serialize_raft_snapshot( &msg );
	CHECK_FALSE( len );

	raft_snapshot->data = tmp;
}

/*
	Tests the serialization of the Raft snapshot into the RMR message buffer
*/
TEST( TestSerializeRaftSnapshot, SerializeRaftSnapshot ) {
	mock()
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( RMR_MAX_RCV_BYTES );

	int len = serialize_raft_snapshot( &msg );
	mock().checkExpectations();

	raft_snapshot_hdr_t *payload = (raft_snapshot_hdr_t *) msg->payload;

	MEMCMP_EQUAL( raft_snapshot->data, RAFT_SNAPSHOT_PAYLOAD_ADDR( msg->payload), dlen );
	UNSIGNED_LONGS_EQUAL( HTONLL( raft_snapshot->last_index ), payload->last_index );
	UNSIGNED_LONGS_EQUAL( HTONLL( raft_snapshot->last_term ), payload->last_term );
	UNSIGNED_LONGS_EQUAL( HTONLL( raft_snapshot->dlen ), payload->dlen );
	UNSIGNED_LONGS_EQUAL( htonl( raft_snapshot->items ), payload->items );
}

/*
	Tests if the RMR payload is reallocated if the snapshot size is greater than the RMR message buffer can hold
*/
TEST( TestSerializeRaftSnapshot, SerializeRaftSnapshotReallocPayload ) {
	mock()
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "rmr_realloc_payload" )
		.withPointerParameter( "old_msg", msg )
		.withParameter( "new_len", RAFT_SNAPSHOT_HDR_SIZE + dlen )
		.withParameter( "copy", 0 )
		.withParameter( "clone", 0 )
		.andReturnValue( msg );

	int len = serialize_raft_snapshot( &msg );
	mock().checkExpectations();

	raft_snapshot_hdr_t *payload = (raft_snapshot_hdr_t *) msg->payload;

	MEMCMP_EQUAL( raft_snapshot->data, RAFT_SNAPSHOT_PAYLOAD_ADDR( msg->payload), dlen );
	UNSIGNED_LONGS_EQUAL( HTONLL( raft_snapshot->last_index ), payload->last_index );
	UNSIGNED_LONGS_EQUAL( HTONLL( raft_snapshot->last_term ), payload->last_term );
	UNSIGNED_LONGS_EQUAL( HTONLL( raft_snapshot->dlen ), payload->dlen );
	UNSIGNED_LONGS_EQUAL( htonl( raft_snapshot->items ), payload->items );
}

/*
	Tests the take_raft_snapshot function, which starts a new snapshot and
	also runs as the child process
*/
TEST_GROUP( TestTakeRaftSnapshot ) {
	pipe_metabuf_t *metabuf = get_raft_metabuf( );
	int dlen = 64;
	unsigned int items = 2;
	index_t last_index = 5;
	term_t last_term = 3;

	void setup() {
		*get_raft_in_progress() = 0;
		metabuf->dlen = dlen;
		metabuf->items = items;
		metabuf->last_index = last_index;
		metabuf->last_term = last_term;
	}

	void teardown() {
		mock().clear();
	}
};

/*
	tests if it allows taking a snapshot while another is in progress
*/
TEST( TestTakeRaftSnapshot, TakeRaftSnapshotInProgress ) {
	*get_raft_in_progress() = 1;	// setting snapshot state to "in progress"
	take_raft_snapshot( );	// should return silently
	CHECK( *get_raft_in_progress() == 1 );	// should still be "in progress"
}

/*
	tests if there is a branch to start the Raft parent thread to receive snapshot data
*/
TEST( TestTakeRaftSnapshot, TakeRaftSnapshotStartParentThread ) {
	*get_raft_in_progress() = 0;

	mock()
		.expectOneCall( "lock_raft_config" );
	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 1 );	// parent process is > 0
	mock()
		.expectOneCall( "unlock_raft_config" );
	mock()
		.expectOneCall( "pthread_create" )
		.withPointerParameter( "__start_routine", (void *)&parent_raft_thread )
		.ignoreOtherParameters()
		.andReturnValue( 0 );

	take_raft_snapshot( );
	mock().checkExpectations();
	CHECK( *get_raft_in_progress() == 1 );	// should still be "in progress"
}

/*
	tests the regular flow of the take_raft_snapshot function
	also tests if "in progress" is set while taking snapshot
*/
TEST( TestTakeRaftSnapshot, TakeRaftSnapshot ) {
	unsigned char *data = (unsigned char *) malloc( dlen );
	if( data == NULL )
		FAIL( "unable to allocate memory for Raft snapshot data" );

	mock()
		.expectOneCall( "lock_raft_config" );
	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 0 );	// child process
	mock()
		.expectOneCall( "unlock_raft_config" );
	mock()
		.expectOneCall( "create_raft_config_snapshot" )
		.withOutputParameterReturning( "data", &data, sizeof(data) )	// this is a pointer of pointer, thus 8 bytes (not 32)
		.withOutputParameterReturning( "raft_metadata", metabuf, sizeof(pipe_metabuf_t *) );	// returns a pointer
	mock()
		.expectOneCall( "write" )
		.withConstPointerParameter( "__buf", metabuf )
		.withUnsignedLongIntParameter( "__n", sizeof(pipe_metabuf_t) )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(pipe_metabuf_t) );
	mock()
		.expectOneCall( "write" )
		.withConstPointerParameter( "__buf", data )
		.withUnsignedLongIntParameter( "__n", dlen )
		.ignoreOtherParameters()
		.andReturnValue( dlen );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EXIT_SUCCESS );

	take_raft_snapshot( );

	mock().checkExpectations();

	CHECK( *get_raft_in_progress() == 1 );
	UNSIGNED_LONGS_EQUAL( dlen, metabuf->dlen );
	UNSIGNED_LONGS_EQUAL( items, metabuf->items );
	UNSIGNED_LONGS_EQUAL( last_index, metabuf->last_index );
	UNSIGNED_LONGS_EQUAL( last_term, metabuf->last_term );
}

/*
	tests error on writing the metadata of the Raft snapshot to the pipe
*/
TEST( TestTakeRaftSnapshot, TakeRaftSnapshotMetaPipeError ) {
	unsigned char *data = (unsigned char *) malloc( dlen );
	if( data == NULL )
		FAIL( "unable to allocate memory for Raft snapshot data" );

	mock()
		.expectOneCall( "lock_raft_config" );
	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 0 );	// child process
	mock()
		.expectOneCall( "unlock_raft_config" );
	mock()
		.expectOneCall( "create_raft_config_snapshot" )
		.withOutputParameterReturning( "data", &data, sizeof(data) )	// this is a pointer of pointer, thus 8 bytes (not 32)
		.withOutputParameterReturning( "raft_metadata", metabuf, sizeof(pipe_metabuf_t *) );	// returns a pointer
	mock()
		.expectOneCall( "write" )
		.withConstPointerParameter( "__buf", metabuf )
		.withUnsignedLongIntParameter( "__n", sizeof(pipe_metabuf_t) )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EX_IOERR );

	take_raft_snapshot( );

	mock().checkExpectations();

	CHECK( *get_raft_in_progress() == 1 );
}

/*
	tests error on writing the serialized data of the Raft snapshot to the pipe
*/
TEST( TestTakeRaftSnapshot, TakeRaftSnapshotDataPipeError ) {
	unsigned char *data = (unsigned char *) malloc( dlen );
	if( data == NULL )
		FAIL( "unable to allocate memory for Raft snapshot data" );

	mock()
		.expectOneCall( "lock_raft_config" );
	mock()
		.expectOneCall( "fork" )
		.andReturnValue( 0 );	// child process
	mock()
		.expectOneCall( "unlock_raft_config" );
	mock()
		.expectOneCall( "create_raft_config_snapshot" )
		.withOutputParameterReturning( "data", &data, sizeof(data) )	// this is a pointer of pointer, thus 8 bytes (not 32)
		.withOutputParameterReturning( "raft_metadata", metabuf, sizeof(pipe_metabuf_t *) );	// returns a pointer
	mock()
		.expectOneCall( "write" )
		.withConstPointerParameter( "__buf", metabuf )
		.withUnsignedLongIntParameter( "__n", sizeof(pipe_metabuf_t) )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)sizeof(pipe_metabuf_t) );
	mock()
		.expectOneCall( "write" )
		.withConstPointerParameter( "__buf", data )
		.withUnsignedLongIntParameter( "__n", dlen )
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "_exit" )
		.withIntParameter( "__status", EX_IOERR );

	take_raft_snapshot( );

	mock().checkExpectations();

	CHECK( *get_raft_in_progress() == 1 );
}

TEST_GROUP( TestRaftParentSnapshot ) {
	pipe_metabuf_t meta;
	unsigned char *data;
	raft_snapshot_t *raft_snapshot = get_raft_snapshot( );
	const index_t last_log_index = 7;
	const term_t last_log_term = 3;
	const size_t dlen = 64;
	const unsigned int items = 2;

	void setup() {
		meta.last_index = last_log_index;
		meta.dlen = dlen;
		meta.items = items;
		meta.last_term = last_log_term;
		raft_snapshot->dlen = 0;
		raft_snapshot->items = 0;
		raft_snapshot->last_index = 0;
		raft_snapshot->last_term = 0;
		data = (unsigned char *) malloc( meta.dlen );
		if( !data )
			FAIL( "unable to allocate memory for xApp snapshot data" );
		memset( data, 0, meta.dlen );

		*get_raft_in_progress() = 1;
	}

	void teardown() {
		mock().clear();
		free( data );
		free( raft_snapshot->data );
		raft_snapshot->data = NULL;
	}
};

/*
	Tests the normal flow of the parent thread
*/
TEST( TestRaftParentSnapshot, RaftParentThread ) {
	// we are simulating the child _exit(EXIT_SUCCESS) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EXIT_SUCCESS, 0 );

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
		.expectOneCall( "compact_raft_log" )
		.withParameter( "last_index", meta.last_index );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_raft_thread( );
	mock().checkExpectations();

	MEMCMP_EQUAL( data, raft_snapshot->data, meta.dlen );
	UNSIGNED_LONGS_EQUAL( meta.last_index, raft_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( meta.last_term, raft_snapshot->last_term );
	UNSIGNED_LONGS_EQUAL( meta.dlen, raft_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( meta.items, raft_snapshot->items );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
}

/*
	tests error on reading the metadata from the pipe
*/
TEST( TestRaftParentSnapshot, RaftParentThreadMetaPipeError ) {
	// we are simulating the child _exit(EXIT_SUCCESS) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EXIT_SUCCESS, 0 );

	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_raft_thread( );
	mock().checkExpectations();

	CHECK_FALSE( raft_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_term );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->items );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
}

/*
	tests error on reading snapshot data from the pipe
*/
TEST( TestRaftParentSnapshot, RaftParentThreadDataPipeError ) {
	// we are simulating the child _exit(EXIT_SUCCESS) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EXIT_SUCCESS, 0 );

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
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_raft_thread( );
	mock().checkExpectations();

	CHECK_TRUE( raft_snapshot->data );	// memory should be allocated
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_term );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->items );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
}

/*
	Tests when the waitpid system call fails
*/
TEST( TestRaftParentSnapshot, RaftParentThreadWaitpidError ) {
	// we fail the first read here to avoid writing unneeded testing code
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.ignoreOtherParameters()
		.andReturnValue( -1 );

	parent_raft_thread( );
	mock().checkExpectations();

	CHECK_FALSE( raft_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_term );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->items );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
}

/*
	Tests when the child exits with an error (exit_status != EXIT_SUCCESS)
*/
TEST( TestRaftParentSnapshot, RaftParentThreadChildExitError ) {
	// we are simulating the child _exit(EX_IOERR) here, sig is 0 (no signal)
	int status = __W_EXITCODE( EX_IOERR, 0 );

	// we fail the first read here to avoid writing unneeded testing code
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_raft_thread( );
	mock().checkExpectations();

	CHECK_FALSE( raft_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_term );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->items );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
}

/*
	Tests when the child exits with a signal error
*/
TEST( TestRaftParentSnapshot, RaftParentThreadChildExitSignal ) {
	// we are simulating the child received a SIGTERM signal (could be any signal)
	int status = __W_EXITCODE( 0, SIGTERM );	// a signal does not have a return status code, thus we use 0

	// we fail the first read here to avoid writing unneeded testing code
	mock()
		.expectOneCall( "read" )
		.withOutputParameterReturning( "__buf", &meta, sizeof(meta) )	// metadata
		.ignoreOtherParameters()
		.andReturnValue( (ssize_t)0 );
	mock()
		.expectOneCall( "waitpid" )
		.withOutputParameterReturning( "__stat_loc", &status, sizeof(status) )
		.ignoreOtherParameters()
		.andReturnValue( 1 );

	parent_raft_thread( );
	mock().checkExpectations();

	CHECK_FALSE( raft_snapshot->data );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->last_term );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->dlen );
	UNSIGNED_LONGS_EQUAL( 0, raft_snapshot->items );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
}

TEST_GROUP( InstallRaftSnapshot ) {
	raft_snapshot_t incoming;
	raft_snapshot_t *local = get_raft_snapshot();
	const index_t last_log_index = 5;
	const term_t last_log_term = 2;
	const unsigned int items = 2;
	const size_t dlen = sizeof(raft_server_snapshot_t) * items;
	int success;

	void setup() {
		local->last_index = 0;
		local->last_term = 0;
		local->items = 0;
		local->dlen = 0;
		local->data = NULL;

		incoming.last_index = last_log_index;
		incoming.last_term = last_log_term;
		incoming.items = items;
		incoming.dlen = dlen;
		incoming.data = (unsigned char *) malloc( dlen );
		if( incoming.data == NULL )
			FAIL( "unable to allocate memory for incoming Raft snapshot data" );
		snprintf( (char *)incoming.data, dlen, "testing bytes of snapshotting data" );

		*get_raft_in_progress() = 0;	// a new snapshot is installed only if there is no other raft snapshot in progress
	}

	void teardown() {
		mock().clear();
		free( incoming.data );
		if( local->data )
			free( local->data );
	}
};

/*
	Tries to install a new raft snapshot while another raft snapshot is in progress
*/
TEST( InstallRaftSnapshot, InstallWhileInProgress ) {
	*get_raft_in_progress() = 1;

	success = install_raft_snapshot( &incoming );
	CHECK_FALSE( success );
	LONGS_EQUAL( 1, *get_raft_in_progress() );
}

/*
	Tests the regular flow of the install_raft_snapshot function
	The corresponding last term and last index are equal in incoming and local snapshots
*/
TEST( InstallRaftSnapshot, InstallRaftSnapshot ) {
	mock()
		.expectOneCall( "commit_raft_config_snapshot" )
		.withPointerParameter( "snapshot", &incoming );

	success = install_raft_snapshot( &incoming );
	mock().checkExpectations();

	CHECK_TRUE( success );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
	UNSIGNED_LONGS_EQUAL( incoming.last_index, local->last_index );
	UNSIGNED_LONGS_EQUAL( incoming.last_term, local->last_term );
	UNSIGNED_LONGS_EQUAL( incoming.items, local->items );
	UNSIGNED_LONGS_EQUAL( incoming.dlen, local->dlen );
	MEMCMP_EQUAL( incoming.data, local->data, incoming.dlen );
}

/*
	Tests the install_raft_snapshot function when last terms differ
	and last indexes are the same
*/
TEST( InstallRaftSnapshot, InstallRaftSnapshotDifferentTerms ) {
	local->last_index = last_log_index;
	mock()
		.expectOneCall( "commit_raft_config_snapshot" )
		.withPointerParameter( "snapshot", &incoming );

	success = install_raft_snapshot( &incoming );
	mock().checkExpectations();

	CHECK_TRUE( success );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
	UNSIGNED_LONGS_EQUAL( incoming.last_index, local->last_index );
	UNSIGNED_LONGS_EQUAL( incoming.last_term, local->last_term );
	UNSIGNED_LONGS_EQUAL( incoming.items, local->items );
	UNSIGNED_LONGS_EQUAL( incoming.dlen, local->dlen );
	MEMCMP_EQUAL( incoming.data, local->data, incoming.dlen );
}

/*
	Tests the install_raft_snapshot function when last indexes differ
	and last terms are the same
*/
TEST( InstallRaftSnapshot, InstallRaftSnapshotDifferentIndexes ) {
	local->last_term = last_log_term;
	mock()
		.expectOneCall( "commit_raft_config_snapshot" )
		.withPointerParameter( "snapshot", &incoming );

	success = install_raft_snapshot( &incoming );
	mock().checkExpectations();

	CHECK_TRUE( success );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
	UNSIGNED_LONGS_EQUAL( incoming.last_index, local->last_index );
	UNSIGNED_LONGS_EQUAL( incoming.last_term, local->last_term );
	UNSIGNED_LONGS_EQUAL( incoming.items, local->items );
	UNSIGNED_LONGS_EQUAL( incoming.dlen, local->dlen );
	MEMCMP_EQUAL( incoming.data, local->data, incoming.dlen );
}

/*
	Tests the install_raft_snapshot function when both the corresponding
	last indexes and the corresponding last terms are the same
	Should not install a new snapshot since the incoming and the local snapshots are the same
*/
TEST( InstallRaftSnapshot, InstallRaftSnapshotEqualTermsAndIndexes ) {
	local->last_index = last_log_index;
	local->last_term = last_log_term;

	success = install_raft_snapshot( &incoming );
	CHECK_FALSE( success );
	LONGS_EQUAL( 0, *get_raft_in_progress() );
}
