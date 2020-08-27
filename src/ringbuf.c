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
 * @file ringbuf.c
 *
 * @brief Static implementation of a generic FIFO ring buffer using a circular array.
 *
 * Reads and writes can be done concurrenlty without locking when a single
 * thread reads while another thread writes.
 * For multi-threaded reads or multi-threaded writes, the corresponding
 * lock configuration is required.
 *
 * This file should be included by all the sources which want to use this
 * ring buffer. Static functions should be inlined in the code by the compiler.
 *
 * @date 26 August 2020.
 *
 * @author Alexandre Huff
 */


#ifndef _RING_BUF_C
#define _RING_BUF_C

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#define RING_NONE	0			/**< no flags on ring buffer. */
#define RING_MP		(1 << 0)	/**< multi-producer ring buffer. */
#define RING_MC		(1 << 1)	/**< multi-consumer ring buffer. */
#define RING_EBLOCK	(1 << 2)	/**< blocks on ring empty. */

/*
	Macro to add milliseconds in a timespec
*/
#define timespec_add_ms(a, ms)					\
	do {										\
		(a).tv_sec += ms / 1000;				\
		(a).tv_nsec += ( ms % 1000 ) * 1000000;	\
		if ( (a).tv_nsec >= 1000000000 ) {		\
			(a).tv_sec++;						\
			(a).tv_nsec -= 1000000000;			\
		}										\
	} while (0)

/**
 * @brief Defines a generic ring buffer.
 *
 * @struct ringbuf
 * @typedef ringbuf_t
 */
typedef struct ringbuf {
	u_int32_t head;				/**< index of the inserting point. */
	u_int32_t tail;				/**< index of the extracting point. */
	u_int32_t size;				/**< total size of the ring (array). */
	u_int32_t mask;				/**< mask for index operations. */
	int flags;					/**< configurations flags. */
	int cond_th;				/**< counter of sleeping threads for cond. */
	pthread_mutex_t *cond_lock;	/**< conditional variable lock (optional). */
	pthread_cond_t *cond;		/**< conditional variable to block on empty ring (optional). */
	pthread_mutex_t *wlock;		/**< write lock for multi-threading producers (optional). */
	pthread_mutex_t *rlock;		/**< read lock for multi-threading consumers (optional). */
	void **data;				/**< ring of pointers to data. */
} ringbuf_t;


/**
 * @brief Frees the ring buffer resources.
 *
 * It is the caller reponsibility to free all data resources prior to call this function.
 *
 * @param ring a pointer to the ring
 * @return On success, returns 1. On error, 0 is returned and @p errno is set to indicate the error.
 */
static inline int ring_free( ringbuf_t *ring ) {
	assert( ring != NULL );

	if( ring->wlock ) {
		if( pthread_mutex_destroy( ring->wlock ) != 0 )
			return 0;	// errno is already set
		free( ring->wlock );
	}

	if( ring->rlock ) {
		if( pthread_mutex_destroy( ring->rlock ) != 0)
			return 0;	// errno is already set
		free( ring->rlock );
	}

	if( ring->cond ) {
		if( pthread_cond_destroy( ring->cond ) != 0 )
			return 0;	// errno is already set
		if( pthread_mutex_destroy( ring->cond_lock ) != 0)
			return 0;	// errno is already set
		free( ring->cond );
		free( ring->cond_lock );
	}

	free( ring->data );
	free( ring );

	return 1;
}

/**
 * @brief Allocates memory and initializes a new ring buffer.
 *
 * Reads and writes can be done concurrenlty without locking when a single
 * thread reads while another thread writes. This is the default configuration.
 * For multi-threaded reads or multi-threaded writes, the corresponding
 * lock configuration is acquired internally.
 *
 * @param size defines the maximum size of the ring (must be power of 2). The usable ring size
 * is actually size-1, in order to differ from a full and an empty ring.
 * @param flags configuration flags can ORed together:
 * 		- RING_NONE: provides default concurrent configuration, unblocking reads and no event notification.
 * 		- RING_MP: multi-producer mode (writes).
 * 		- RING_MC: multi-consumer mode (reads).
 * 		- RING_EBLOCK: blocks consumer reads while the ring is empty.
 * @return On success, returns a pointer to a ringbuf_t*. On error, NULL is returned and @p errno is set to indicate the error.
 */
static inline ringbuf_t *ring_create( u_int32_t size, int flags ) {
	int error;
	ringbuf_t *ring;

	if( size == 0)
		return NULL;

	if( size < 16 )
		size = 16;	// smallest size of the ring

	if( ( size & (size - 1) ) != 0 ) {	// ensuring that size is power of two
		errno = EINVAL;
		return NULL;
	}

	ring = (ringbuf_t *) malloc( sizeof( ringbuf_t ) );
	if( ring == NULL )
		return NULL;

	ring->head = 0;
	ring->tail = 0;
	ring->size = size;
	ring->mask = size - 1;
	ring->flags = flags;

	ring->data = (void **) malloc( size * sizeof( void **) );
	if( ring->data == NULL ) {
		free( ring );	// does not change errno
		return NULL;
	}
	memset(ring->data, 0, size * sizeof( void **) );

	ring->wlock = NULL;
	ring->rlock = NULL;

	if( flags & RING_MP ) {	// multiple producer cond_th
		ring->wlock = (pthread_mutex_t *) malloc( sizeof( pthread_mutex_t ) );
		if( ring->wlock == NULL ) {
			error = errno;
			ring_free( ring );
			errno = error;
			return NULL;
		}
		pthread_mutex_init( ring->wlock, NULL );
	}

	if( flags & RING_MC ) {	// multiple consumer cond_th
		ring->rlock = (pthread_mutex_t *) malloc( sizeof( pthread_mutex_t ) );
		if( ring->rlock == NULL ) {
			error = errno;
			ring_free( ring );
			errno = error;
			return NULL;
		}
		pthread_mutex_init( ring->rlock, NULL );
	}

	ring->cond_lock = NULL;
	ring->cond = NULL;
	ring->cond_th = 0;

	if( flags & RING_EBLOCK ) {
		ring->cond_lock = (pthread_mutex_t *) malloc( sizeof( pthread_mutex_t ) );
		if( ring->cond_lock == NULL ) {
			error = errno;
			ring_free( ring );
			errno = error;
			return NULL;
		}
		pthread_mutex_init( ring->cond_lock, NULL );

		ring->cond = (pthread_cond_t *) malloc( sizeof( pthread_cond_t ) );
		if( ring->cond == NULL ) {
			error = errno;
			ring_free( ring );
			errno = error;
		}
		pthread_cond_init( ring->cond, NULL );
	}

	return ring;
}

/**
 * @brief Returns the current number of elements stored in the ring.
 *
 * @param ring a pointer to the ring
 * @return the number of elements in the ring
 */
static inline u_int32_t ring_count( ringbuf_t *ring ) {
	assert( ring != NULL );
	/*
		The ring implementation ensures that the distance between head and tail is always in the range of 0 and size-1.
		Thus, there is no problem in doing substraction of 2 unsigned integers modulo 32 bit. The reason is that they overflow naturally and at
		any time, the result is between 0 and SIZE-1 even if the first index has overflowed (i.e. it is smaller than the second index)
	*/
	return ( ring->head - ring->tail );
}

/**
 * @brief Returns the number of free elements in the ring.
 *
 * @param ring a pointer to the ring
 * @return the number of free elements in the ring
 */
static inline u_int32_t ring_count_free( ringbuf_t *ring ) {
	assert( ring != NULL );
	/*
		The ring implementation ensures that the distance between head and tail is always in the range of 0 and size-1.
		Thus, there is no problem in doing addition and substraction of 2 unsigned integers modulo 32 bit. The reason is that they
		overflow naturally and at any time, the result is between 0 and SIZE-1 even if a given index has overflowed
	*/
	return ( ring->mask + ring->tail - ring->head );
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
static inline float ring_stats( ringbuf_t *ring ) {
	return ring_count( ring) / (float)ring->mask;
}

/**
 * @brief Returns the size of the ring.
 *
 * @param ring a pointer to the ring.
 * @return a u_int32_t indicating the maximum usable size of the ring (size-1).
 */
static inline u_int32_t ring_size( ringbuf_t *ring ) {
	return ring->size - 1;
}

/**
 * @brief Inserts a new data into the ring buffer.
 *
 * This is a zero-copy operation, only the pointer is stored into the ring.
 *
 * @param ring a pointer to the ring
 * @param data a pointer to the data to store in the ring
 * @return On success, returns 1. On error, 0 is returned.
 */
static inline int ring_insert( ringbuf_t *ring, void *data ) {
	int ret = 1;
	assert( ring != NULL );
	assert( data != NULL );

	if( ring->wlock )
		pthread_mutex_lock( ring->wlock );

	if( ring_count_free( ring ) > 0 ) {	// only insert if ring is not full
		/*
			we have to mask the ring head here since we use the full range of the usigned int 32 bit indexes
			we also can use AND with mask (size-1) instead of MOD(size) here whenever size is power of 2
		*/
		ring->data[ring->head & ring->mask] = data;
		ring->head++;	// this will overflow naturally to 0 at some point

		if( ring->cond ) {
			pthread_mutex_lock( ring->cond_lock );
			if( ring->cond_th > 0 ) {
				ring->cond_th--;
				pthread_cond_signal( ring->cond );
			}
			pthread_mutex_unlock( ring->cond_lock );
		}

	} else {
		ret = 0;
	}

	if( ring->wlock )
		pthread_mutex_unlock( ring->wlock );

	return ret;
}

/**
 * @brief Extracts an element from the ring buffer.
 *
 * Also sets the content of the extracted index to NULL.
 *
 * @param ring a pointer to the ring
 * @param timeout time in milliseconds to wait before giving up waiting. Timeout is only
 * 		applied if the ring has been created with the RING_EBLOCK flag. Otherwise,
 * 		this function returns immediately. A timeout of 0 (zero) blocks indefinitely.
 * @return On success, returns a pointer to the extracted data.
 * On ring empty, returns NULL.
 */
static inline void *ring_extract_timeout( ringbuf_t *ring, u_int32_t timeout ) {
	void *ptr = NULL;
	struct timespec ts;
	int ret;

	assert( ring != NULL );

	if( ring->rlock )
		pthread_mutex_lock( ring->rlock );

	while( 1 ) {
		if( ring->cond && ( ring_count( ring ) == 0 ) ) { // ring_count bail out quickly
			ret = -1;
			pthread_mutex_lock( ring->cond_lock );
			if( ring_count( ring ) ==  0 ) {	// the producer might have inserted while the consumer was acquiring the lock
				ring->cond_th++;
				/*
					only one thread gets here at a time, so there is no need to release rlock
					all other threads will block trying to acquire rlock
				*/
				if( timeout == 0 ) {	// there is no timeout
					pthread_cond_wait( ring->cond, ring->cond_lock );

				} else {
					clock_gettime( CLOCK_REALTIME, &ts );
					timespec_add_ms( ts, timeout );
					while( ret != 0 && ret != ETIMEDOUT )
						ret = pthread_cond_timedwait( ring->cond, ring->cond_lock, &ts );
					/*
						we cannot unlock here, when timeout is too small it seems that
						pthread_cond_timedwait does not acquires the lock, i.e. it has
						timedout before pthread_cond_timedwait could run
					*/
				}
			}
			pthread_mutex_unlock( ring->cond_lock );
			if( ret == ETIMEDOUT )	// needs to keep this here due to unlocking issues
				break;
		}

		if ( ring_count( ring ) > 0 ) {
			/*
				we have to mask the ring tail here since we use the full range of the usigned int 32 bit indexes
				we also can use AND with mask (size-1) instead of MOD(size) here whenever size is power of 2
			*/
			ptr = ring->data[ring->tail & ring->mask];
			ring->data[ring->tail & ring->mask] = NULL;  // forcing to be NULL in case of trying to get an element that is out of range
			ring->tail++;	// this will overflow naturally to 0 at some point
			break;

		} else {	// ring is empty and it is non-blocking
			break;
		}
	}

	if( ring->rlock )
		pthread_mutex_unlock( ring->rlock );

	return ptr;
}

/**
 * @brief Extracts an element from the ring buffer.
 *
 * Also sets the content of the extracted index to NULL.
 * Blocks indefinitely if the ring has been created with the RING_EBLOCK flag.
 *
 * @param ring a pointer to the ring
 * @return On success, returns a pointer to the extracted data.
 * On ring empty, returns NULL or blocks up to next insertion.
 */
static inline void *ring_extract( ringbuf_t *ring ) {
	return ring_extract_timeout( ring, 0 );	// this is only a wrapper function
}

/**
 * @brief Returns the current element of the ring without extracting it.
 *
 * This function does not blocks even when the RING_EBLOCK is set.
 *
 * @param ring a pointer to the ring
 * @return On success, returns a pointer to the current data.
 * On ring empty, returns NULL.
 */
static inline void *ring_get( ringbuf_t *ring ) {
	void *ptr;

	assert( ring != NULL );

	if( ring->rlock )
		pthread_mutex_lock( ring->rlock );

	/*
		we have to mask the ring tail here since we use the full range of the usigned int 32 bit indexes
		we also can use AND with mask (size-1) instead of MOD(size) here whenever size is power of 2
	*/
	ptr = ring->data[ring->tail & ring->mask];

	if( ring->rlock )
		pthread_mutex_unlock( ring->rlock );

	return ptr;
}

/**
 * @brief Checks if the ring is empty.
 *
 * @param ring a pointer to the ring.
 * @return On empty returns 1, otherwise returns 0.
 */
static inline int ring_is_empty( ringbuf_t *ring ) {
	return !ring_count( ring );
	// this is one example of returning 0 or 1
}

/**
 * @brief Checks if the ring is full.
 *
 * @param ring a pointer to the ring.
 * @return On full returns 1, otherwise returns 0.
 */
static inline int ring_is_full( ringbuf_t *ring ) {
	return !!( ring_count_free( ring ) == 0 );
	// this is another example of returning 0 or 1
}


#endif
