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
 * @file ringbuf.h
 *
 * @brief Defines typedefs and macros for a generic FIFO ring buffer based on a
 * circular array implementation.
 *
 * @date 28 August 2020.
 *
 * @author Alexandre Huff
 */


#ifndef _RING_BUF_H
#define _RING_BUF_H

#define RING_NONE	0			/**< no flags on ring buffer. */
#define RING_MP		(1 << 0)	/**< multi-producer ring buffer. */
#define RING_MC		(1 << 1)	/**< multi-consumer ring buffer. */
#define RING_EBLOCK	(1 << 2)	/**< blocks on ring empty. */


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


#endif
