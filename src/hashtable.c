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
	Mnemonic:	hashtable.c
	Abstract:	Implements a generic static hashtable using chain hashing

	Date:		27 January 2020
	Author:		Alexandre Huff
*/


#ifndef _HASHTABLE_C
#define _HASHTABLE_C

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hashtable.h"
#include "logger.h"


static inline unsigned long hash_code( hkey_t key_type, const char *key, size_t size ) {
	const char *p;
	unsigned long hash;

	if( key_type == NUMERIC_KEY )
		return *((unsigned long *) key) % size;

	hash = 7;
	for( p = key; *p; p++ ) {
		hash = hash * 31 + (int) *p;
	}

	return hash % size;
}

static inline int equals( hkey_t key_type, htelem_t *elem, const char *key ) {
	if( key_type == NUMERIC_KEY && ( elem->nkey == *((unsigned long *) key) ) )
		return 1;

	if( key_type == STRING_KEY )
		return !( strcmp( elem->skey, (char *) key) );  // strcmp returns 0 if match, so we have to invert the result

	return 0;	// in case of numeric is different
}

/*
	Allocates memory for a new hashtable

	It can be allocated to used or numeric keys, or string keys
	If numeric keys, they must point to "unsigned long" variables

	They pointer to the key type must be of the same type in
	all subsequent operations with the returned pointer
*/
static inline hashtable_t *hashtable_new( hkey_t key_type, size_t size ) {
	hashtable_t *table;

	table = (hashtable_t *) malloc( sizeof( hashtable_t ) );
	if( table != NULL ) {
		table->size = size;

		table->elem_list = malloc( size * sizeof( htelem_t ) );
		if( table->elem_list == NULL ) {
			free( table );
			return NULL;
		}

		memset( table->elem_list, 0, size * sizeof( htelem_t ) );

		table->key_type = key_type;

		return table;
	}

	return NULL;
}

/*
	Frees the hash table memory
*/
static inline void hashtable_free( hashtable_t *table ) {
	if( table != NULL ) {
		if( table->elem_list )
			free( table->elem_list );
		free( table );
	}
}

/*
	Stores the data based on its key in the hastable table

	If the key was added previsously, then the stored data (*value) is changed with the new one

	Note 1: zero-copy operation
	Note 2: It is the caller resposibility to check if the new value's pointer is not
			as same as the stored one, leading to memory likage

	Returns 1 if key:value has been inserted in the table, 0 otherwise

	Slight change for RFT: In case of colision the new element is stored at the end of the list,
	this allows getting log_entries faster when compacting logs, since the log entries with
	lower indexes will be at the beginning of the list
*/
static inline int hashtable_insert( hashtable_t *table, const char *key, void *value ) {
	unsigned long hash;
	htelem_t *elem;
	htelem_t *head = NULL;

	if( table == NULL ) {
		errno = EINVAL;
		logger_error( "table argument cannot be null to insert an element into the hashtable" );
		return 0;
	}
	if( key == NULL ) {
		errno = EINVAL;
		logger_error( "key argument cannot be null to insert an element into the hashtable" );
		return 0;
	}

	hash = hash_code( table->key_type, key, table->size );

	elem = table->elem_list[hash];
	while( elem ) {		// check if the key was previously stored (only changing data)
		if( equals( table->key_type, elem, key ) )
			break;

		head = elem;	// we are going to add the element at the end of the list
		elem = elem->next;
	}

	if( elem == NULL ) {

		elem = (htelem_t *) malloc( sizeof( htelem_t ) );
		if( elem == NULL ) {
			logger_error( "unable to allocate a new hashtable element: %s", strerror( errno) );
			return 0;
		}

		if( table->key_type == NUMERIC_KEY ) {
			elem->nkey = *((unsigned long *) key);
			elem->skey = NULL;	// not used
		} else {
			elem->nkey = 0;		// not used
			elem->skey = strdup( key ); // returns NULL if insufficient memory
			if( !elem->skey ) {
				free( elem );
				return 0;
			}
		}

		/* adding to the end of the list */
		elem->next = NULL;
		elem->prev = head;
		if( elem->prev == NULL )		// if this is the first element, head will be NULL
			table->elem_list[hash] = elem;
		else
			elem->prev->next = elem;
	}

	elem->value = value;

	return 1;
}

/*
	Removes element pointed by key from the hashtable table

	Returns the pointer of the value stored by the removed key,
	if the key was not found, then NULL is returned
	It is the caller resposibility to free the returned value
*/
static inline void *hashtable_delete( hashtable_t *table, const char *key ) {
	unsigned long hash;
	htelem_t *elem;
	char *value;

	if( table == NULL ) {
		errno = EINVAL;
		logger_error( "table argument cannot be null to delete an element from hashtable" );
		return NULL;
	}
	if( key == NULL ) {
		errno = EINVAL;
		logger_error( "key argument cannot be null to delete an element from hashtable" );
		return NULL;
	}

	hash = hash_code( table->key_type, key, table->size );

	elem = table->elem_list[hash];
	while( elem ) {
		if( equals( table->key_type, elem, key ) ) {

			if( elem->prev )	// it's not the first element
				elem->prev->next = elem->next;
			else
				table->elem_list[hash] = elem->next;

			if( elem->next )
				elem->next->prev = elem->prev;

			value = elem->value;
			free( (char *) elem->skey );
			free( elem );

			return value;
		}

		elem = elem->next;
	}

	return NULL;	// element not found
}

/*
	Returns the value associated with the key in the hashtable table
*/
static inline void *hashtable_get( hashtable_t *table, const char *key ) {
	htelem_t *elem;
	unsigned long hash;

	if( table == NULL ) {
		errno = EINVAL;
		logger_error( "table argument cannot be null to get an element from hashtable" );
		return NULL;
	}
	if( key == NULL ) {
		errno = EINVAL;
		logger_error( "key argument cannot be null to get an element from hashtable" );
		return NULL;
	}

	hash = hash_code( table->key_type, key, table->size );
	elem = table->elem_list[hash];
	while( elem ) {
		if( equals( table->key_type, elem, key ) )
			return elem->value;

		elem = elem->next;
	}

	return NULL;
}

/*
	Iterates over all keys stored in the table
	This function calls the handler() callback function for each key,
	allowing the user to manage all the elements in the hashtable

	For example, it is safe to write a callback function that frees all
	elements stored in the table by using the hashtable_delete() function
	After remove all elements, the hashtable memory can be released by
	calling the hashtable_free() function
*/
static inline void hashtable_foreach_key( hashtable_t *table, void (* handler)( hashtable_t *table, const char *key ) ) {
	size_t i;
	htelem_t *elem;
	htelem_t *ptr;

	if( table != NULL && handler != NULL ) {
		for( i = 0; i < table->size; i++ ) {	// traversing all the list
			elem = table->elem_list[i];
			while( elem ) {						// cleaning all elements from the list
				ptr = elem;
				elem = elem->next;				// pointing to the next element

				if( ptr->skey )					// if NULL it is a numeric key
					handler( table, ptr->skey );
				else {
					handler( table, (char *) &ptr->nkey );
				}
			}
		}
	}
}

#endif
