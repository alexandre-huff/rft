// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2023 Alexandre Huff Intellectual Property.

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
	Mnemonic:	redis.c
	Abstract:	Implements a wrapper of redis calls to replicate the state of xApps

	Date:		13 March 2023
	Author:		Alexandre Huff
*/

#ifndef _REDIS_C
#define _REDIS_C

#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include <hiredis/hiredis.h>
#include <logger.h>


/* ##################### Synchronous API ##################### */

/*
    Connects to a Redis server

    Returns a pointer to redisContext on success, nill on error.
*/
redisContext *redis_sync_init( const char *host, int port ) {
	redisContext *c;

    struct timeval timeout = { 1, 500000 }; // 1.5 seconds

	c = redisConnectWithTimeout( host, port, timeout );
	if( c == NULL || c->err ) {
		if (c) {
			logger_error( "unable to connect to redis server %s:%d, error: %s", c->tcp.host, c->tcp.port, c->errstr );
			redisFree(c);
		} else {
			logger_error( "unable to allocate redis context" );
		}
        return NULL;
	}

	logger_debug( "connected to redis server %s:%d", c->tcp.host, c->tcp.port );

	return c;
}

void redis_sync_disconnect( redisContext *c ) {
    if( c ) {
        logger_debug( "disconnecting from redis server %s:%d", c->tcp.host, c->tcp.port );
        redisFree( c );
    }
}

/*
	Generic function to retrieve the current value from a key

	NOTE: is the caller resposibility to free the returned object
		  with the freeReplyObject() function
*/
redisReply *redis_sync_get_value( redisContext *c, char *key ) {
	redisReply *reply;

	reply = redisCommand( c, "GET %s", key );
	if( reply == NULL ) {
		logger_error( "command error: %s", c->errstr );
		/*
			Once an error is returned the context cannot be reused and you should set up a new connection
			An error message could be returned, but we chose to exit the application
		*/
		if( redisReconnect( c ) != REDIS_OK ) {
			logger_fatal( "unable to reconnect using the previous redis context" );
			exit( 1 );
		}
	}

	return reply;
}

/*
    This function sets the value only if the key is not defined in the Redis server

    Returns 1 on success, 0 on error
*/
int redis_sync_set_if_no_key( redisContext *c, char *key, char *value ) {
    int ret = 0;
	redisReply *reply;

    reply = redisCommand( c, "SET %s %s NX", key, value );
	if( reply == NULL ) {
		logger_error( "command error: %s", c->errstr );
		/*
			Once an error is returned the context cannot be reused and you should set up a new connection
			An error message could be returned, but we chose to exit the application
		*/
		if( redisReconnect( c ) != REDIS_OK ) {
			logger_error( "unable to reconnect using the previous redis context" );
			exit( 1 );
		}
	}

    if( reply->len ) { // expecting "OK"
		if( strcmp( reply->str, "OK" ) == 0 ) {
            ret = 1;
        }
    }
    freeReplyObject( reply );

	return ret;
}

/*
    This function sets the value of the key in the Redis server

    Returns 1 on success, 0 on error
*/
int redis_sync_set_key( redisContext *c, char *key, char *value ) {
    int ret = 0;
	redisReply *reply;

    reply = redisCommand( c, "SET %s %s", key, value );
	if( reply == NULL ) {
		logger_error( "command error: %s", c->errstr );
		/*
			Once an error is returned the context cannot be reused and you should set up a new connection
			An error message could be returned, but we chose to exit the application
		*/
		if( redisReconnect( c ) != REDIS_OK ) {
			logger_fatal( "unable to reconnect using the previous redis context" );
			exit( 1 );
		}
	}

    if( reply->len ) { // expecting "OK"
		if( strcmp( reply->str, "OK" ) == 0 ) {
            ret = 1;
        }
    }
    freeReplyObject( reply );

	return ret;
}

/*
	Safely deletes the enpoint of the key on Redis server

	This function implements a transaction that only removes the key if it
	stores the corresponding enpoint in Redis.

	Trying to execute the following:
		WATCH mykey
		val = GET mykey
		if (val == endpoint) then
			MULTI
			DEL mykey
			EXEC
		endif

	Returns:
		1 on deletion success;
		1 when another replica already changed the endpoint of the key;
		0 on error.
*/
int redis_sync_safe_ep_del( redisContext *c, char *key, char *endpoint ) {
	redisReply *multi;
	redisReply *delete;
	redisReply *exec;

	redisReply *reply = redisCommand( c, "WATCH %s", key );
	if( reply == NULL ) {
		logger_error( "command error: %s", c->errstr );
		/*
			Once an error is returned the context cannot be reused and you should set up a new connection
			An error message could be returned, but we chose to exit the application
		*/
		if( redisReconnect( c ) != REDIS_OK ) {
			logger_fatal( "unable to reconnect using the previous redis context" );
			exit( 1 );
		}
	}
	if( reply->len ) { // expecting "OK"
		if( strcmp( reply->str, "OK" ) != 0 ) {
			freeReplyObject( reply );
            return 0;
        }
    }
	freeReplyObject( reply );

	reply = redis_sync_get_value( c, key );
	if( reply == NULL ) {
		logger_error( "unable to get key '%s'", key );
		return 0;
	}
	if( reply->str == NULL ) {
		logger_error( "redis did not return a string for key '%s'", key);
		freeReplyObject( reply );
		return 0;
	}

	// in case another replica already became the leader, the enpoint was changed and is different of our self_id
	if( strcmp( reply->str, endpoint ) != 0 ) {
		freeReplyObject( reply );

		reply = redisCommand( c, "UNWATCH" );	// watch is no longer required
		if( reply == NULL ) {
			logger_error( "unable to call UNWATCH on Redis server" );
			return 0;
		}
		freeReplyObject( reply );
		return 1;	// we can consider this as success as another replica became the leader
	}

	// if we reach here, then the key stored in redis is our target endpoint
	multi = redisCommand( c, "MULTI" );
	if( multi == NULL ) {
		logger_error( "unable to call MULTI on Redis server" );
		return 0;
	}
	if( multi->str && ( strcmp( multi->str, "OK" ) != 0 ) ) {
		logger_error( "Redis command MULTI did not return OK" );
		freeReplyObject( multi );
		return 0;
	}

	delete = redisCommand( c, "DEL %s", key );
	if( delete == NULL ) {
		logger_error( "unable to call DEL on Redis server" );
		return 0;
	}
	if( delete->str && ( strcmp( delete->str, "QUEUED" ) != 0 ) ) {
		logger_error( "unable to delete key '%s' from Redis server", key );
		freeReplyObject( delete );
		return 0;
	}

	exec = redisCommand( c, "EXEC");
	if( exec == NULL ) {
		logger_error( "unable to call EXEC on Redis server" );
		return 0;
	}
	if( exec->elements != 1 ) {
		logger_error( "EXEC command did not execute successfully");
		freeReplyObject( delete );
		return 0;
	}

	logger_info( "key '%s' was successfully deleted from Redis server", key );

	freeReplyObject( multi );
	freeReplyObject( delete );
	freeReplyObject( exec );

	return 1;	// success of transaction
}

#endif
