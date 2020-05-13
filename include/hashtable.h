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
	Mnemonic:	hashtable.h
	Abstract:	Defines header file of typedefs for a generic static hashtable

	Date:		12 May 2020
	Author:		Alexandre Huff
*/

#ifndef _HASHTABLE_H
#define _HASHTABLE_H

/*
	Hash key type
*/
typedef enum hkey {
	NUMERIC_KEY = 0,
	STRING_KEY
} hkey_t;

/*
	Defines a generic hashtable element accepting numeric and string keys
*/
typedef struct htelem {
	struct htelem *prev;
	struct htelem *next;
	char *skey;			// string key
	unsigned long nkey;	// numeric key
	void *value;
} htelem_t;

/*
	Defines a generic hash table that accepts numeric and string keys
*/
typedef struct hashtable {
	size_t size;			// size of the hash table (use a prime number)
	htelem_t **elem_list;	// pointer to the linked list of elements with the same hash
	hkey_t key_type;
} hashtable_t;

#endif
