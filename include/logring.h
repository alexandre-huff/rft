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

/**
 * @file logring.h
 *
 * @brief Defines the structure for the Log Entries RING.
 *
 * @date 28 August 2020.
 *
 * @author Alexandre Huff
 */


#ifndef _LOGRING_H
#define _LOGRING_H


/**
 * @brief Defines a generic ring to store log entries.
 *
 * @struct logring
 * @typedef logring_t
 */
typedef struct logring {
	u_int32_t head;		/**< index of the inserting point. */
	u_int32_t tail;		/**< index of the extracting point. */
	u_int32_t size;		/**< total size of the ring (array). */
	u_int32_t mask;		/**< mask for index operations. */
	void **data;		/**< ring of pointers to data. */
} logring_t;


#endif
