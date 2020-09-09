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

/**
 * @file logring.c
 *
 * @brief Static implementation of the Log Entries RING using a circular array.
 *
 * This file provides a simple implementation of a ring buffer to store and manage
 * both, Raft and xApp log entries. This is a specific implementation of a ring
 * and thus, should not be used with other purpose. No locks guaranties are provided
 * by this implementation, which relies on the locks acquired and released by the
 * caller function in the log.c module.
 *
 * A generic implementation of a ring buffer is provided by ringbuf.c
 *
 * This file should be included by all the sources which want to use this
 * ring buffer. Static functions should be inlined in the code by the compiler.
 *
 * @date 28 August 2020.
 *
 * @author Alexandre Huff
 */


#ifndef _LOGRING_C
#define _LOGRING_C

#include <assert.h>
#include <errno.h>

#include "logring.h"


/**
 * @brief Frees the ring resources.
 *
 * It is the caller reponsibility to free all data resources prior to call this function.
 *
 * @param ring a pointer to the ring
 * @return On success, returns 1. On error, 0 is returned and @p errno is set to indicate the error.
 */
static inline void log_ring_free( logring_t *ring ) {
	assert( ring != NULL );

	if( ring->data )
		free( ring->data );
	free( ring );
}

/**
 * @brief Allocates memory and initializes a new log entries ring.
 *
 * @param size defines the maximum size of the ring (must be power of 2). The usable ring size
 * is actually size-1, in order to differ from a full and an empty ring.
 * @return On success, returns a pointer to a logring_t*. On error, NULL is returned and @p errno is set to indicate the error.
 */
static inline logring_t *log_ring_create( u_int32_t size ) {
	logring_t *ring;

	if( size == 0) {
		errno = EINVAL;
		return NULL;
	}

	if( size < 16 )
		size = 16;	// smallest size of the ring

	if( ( size & (size - 1) ) != 0 ) {	// ensuring that size is power of two
		errno = EINVAL;
		return NULL;
	}

	ring = (logring_t *) malloc( sizeof(logring_t) );
	if( ring == NULL )
		return NULL;	// errno is already set

	ring->head = 0;
	ring->tail = 0;
	ring->size = size;
	ring->mask = size - 1;

	ring->data = (void **) malloc( size * sizeof(void *) );
	if( ring->data == NULL ) {
		free( ring );	// does not change errno
		return NULL;
	}
	memset(ring->data, 0, size * sizeof(void *) );

	return ring;
}

/**
 * @brief Returns the number of log entries stored in the ring.
 *
 * @param ring a pointer to the ring
 * @return the number of log entries in the ring
 */
static inline u_int32_t log_ring_count( logring_t *ring ) {
	assert( ring != NULL );
	/* can use AND with mask (size-1) instead of MOD(size) here whenever size is power of 2 */
	return ( ring->head - ring->tail ) & ring->mask;
}

/**
 * @brief Returns usage stats of the ring.
 *
 * The usage stats represents the proportional usage of the ring according
 * to its real usable size, which is size-1 to differentiate from a full and
 * an empty ring.
 *
 * @param ring a pointer to the ring.
 * @return a float between 0 and 1 (0 means empty and 1 means full).
 */
static inline float log_ring_stats( logring_t *ring ) {
	return log_ring_count( ring ) / (float)ring->mask;
}

/**
 * @brief Inserts a new log entry into the ring.
 *
 * This is a zero-copy operation, only the pointer is stored into the ring.
 *
 * Assumes that the log lock is acquired by the caller
 *
 * @param ring a pointer to the ring
 * @param entry a pointer to the log entry to store in the ring
 * @return On success, returns 1. On error, 0 is returned (ring full).
 */
static inline int log_ring_insert( logring_t *ring, log_entry_t *entry ) {
	u_int32_t next;
	assert( ring != NULL );
	assert( entry != NULL );

	/* can use AND with mask (size-1) instead of MOD(size) here whenever size is power of 2 */
	next = ( ring->head + 1 ) & ring->mask;

	if( next != ring->tail ) {	// ring is full when (head + 1) == tail

		ring->data[ring->head] = entry;
		ring->head = next;

		return 1;
	}

	return 0;
}

/**
 * @brief Extracts the oldest log entry from the ring.
 *
 * The oldest log corresponds to the lowest log index (FIFO order)
 *
 * Also sets the content of the extracted index in the ring to NULL.
 *
 * Assumes that the log lock is acquired by the caller
 *
 * @param ring a pointer to the ring
 * @return On success, returns a pointer to the extracted log entry.
 * On ring empty, returns NULL.
 */
static inline log_entry_t *log_ring_extract( logring_t *ring ) {
	log_entry_t *ptr = NULL;

	assert( ring != NULL );

	if ( ring->tail != ring->head ) {	// ring is empty when tail == head

		ptr = (log_entry_t *) ring->data[ring->tail];
		ring->data[ring->tail] = NULL;  // forcing to be NULL in case of trying to get an element that is out of range
		ring->tail = ( ring->tail + 1 ) & ring->mask;

	}

	return ptr;
}


/**
 * @brief Extracts the newest log entry from the ring (reverse).
 *
 * The newest log corresponds to the highest log index (LIFO order)
 *
 * Also sets the content of the extracted index in the ring to NULL.
 *
 * Assumes that the log lock is acquired by the caller
 *
 * @param ring a pointer to the ring
 * @return On success, returns a pointer to the extracted log entry.
 * On ring empty, returns NULL.
 */
static inline log_entry_t *log_ring_extract_r( logring_t *ring ) {
	log_entry_t *ptr = NULL;

	assert( ring != NULL );

	if ( ring->tail != ring->head ) {	// ring is empty when tail == head

		ring->head = ( ring->head - 1 ) & ring->mask;	// reverse
		ptr = (log_entry_t *) ring->data[ring->head];
		ring->data[ring->head] = NULL;  // forcing to be NULL in case of trying to get an element that is out of range

	}

	return ptr;
}

/**
 * @brief Returns the log entry according to its log index
 *
 * This function does not extract the log entry from the ring.
 *
 * @param ring a pointer to the ring
 * @param log_index the index of a raft/xapp log entry to retrieve.
 * @return On success, returns a pointer to the current data.
 * On error (not found), returns NULL.
 */
static inline log_entry_t *log_ring_get( logring_t *ring, index_t log_index ) {
	log_entry_t *ptr;
	index_t tidx;	// tail log index stored in the ring (the lowest)
	index_t hidx;	// head log index stored in the ring (the highest)
	index_t offset;

	assert( ring != NULL );

	ptr = (log_entry_t *) ring->data[ring->tail];
	if( ptr == NULL )
		return NULL;
	tidx = ptr->index;

	/*
		this does not return NULL, because the head has been moved
		forward and therefore the head will certainly have a value
	*/
	ptr = (log_entry_t *) ring->data[(ring->head - 1) & ring->mask];
	hidx = ptr->index;

	if( ( log_index >= tidx ) && ( log_index <= hidx )  ) {
		offset = log_index - tidx;
		return (log_entry_t *) ring->data[ ( ring->tail + offset ) & ring->mask ];
	}

	return NULL;	// log_index is out of range
}


#endif
