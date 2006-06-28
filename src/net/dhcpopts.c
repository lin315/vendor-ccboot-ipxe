/*
 * Copyright (C) 2006 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdint.h>
#include <byteswap.h>
#include <errno.h>
#include <malloc.h>
#include <assert.h>
#include <gpxe/list.h>
#include <gpxe/dhcp.h>

/** @file
 *
 * DHCP options
 *
 */

/** List of registered DHCP option blocks */
static LIST_HEAD ( option_blocks );

/**
 * Obtain value of a numerical DHCP option
 *
 * @v option		DHCP option, or NULL
 * @ret value		Numerical value of the option, or 0
 *
 * Parses the numerical value from a DHCP option, if present.  It is
 * permitted to call dhcp_num_option() with @c option set to NULL; in
 * this case 0 will be returned.
 *
 * The caller does not specify the size of the DHCP option data; this
 * is implied by the length field stored within the DHCP option
 * itself.
 */
unsigned long dhcp_num_option ( struct dhcp_option *option ) {
	unsigned long value = 0;
	uint8_t *data;

	if ( option ) {
		/* This is actually smaller code than using htons()
		 * etc., and will also cope well with malformed
		 * options (such as zero-length options).
		 */
		for ( data = option->data.bytes ;
		      data < ( option->data.bytes + option->len ) ; data++ )
			value = ( ( value << 8 ) | *data );
	}
	return value;
}

/**
 * Calculate length of a DHCP option
 *
 * @v option		DHCP option
 * @ret len		Length (including tag and length field)
 */
static inline unsigned int dhcp_option_len ( struct dhcp_option *option ) {
	if ( ( option->tag == DHCP_END ) || ( option->tag == DHCP_PAD ) ) {
		return 1;
	} else {
		return ( option->len + 2 );
	}
}

/**
 * Find DHCP option within block of raw data
 *
 * @v tag		DHCP option tag to search for
 * @v data		Data block
 * @v len		Length of data block
 * @ret option		DHCP option, or NULL if not found
 *
 * Searches for the DHCP option matching the specified tag within the
 * block of data.  Encapsulated options may be searched for by using
 * DHCP_ENCAP_OPT() to construct the tag value.
 *
 * This routine is designed to be paranoid.  It does not assume that
 * the option data is well-formatted, and so must guard against flaws
 * such as options missing a @c DHCP_END terminator, or options whose
 * length would take them beyond the end of the data block.
 *
 * Searching for @c DHCP_PAD or @c DHCP_END tags, or using either @c
 * DHCP_PAD or @c DHCP_END as the encapsulator when constructing the
 * tag via DHCP_ENCAP_OPT() will produce undefined behaviour.
 */
static struct dhcp_option * find_dhcp_option_raw ( unsigned int tag,
						   void *data, size_t len ) {
	struct dhcp_option *option = data;
	ssize_t remaining = len;
	unsigned int option_len;

	assert ( tag != DHCP_PAD );
	assert ( tag != DHCP_END );
	assert ( DHCP_ENCAPSULATOR ( tag ) != DHCP_END );

	while ( remaining ) {
		/* Check for explicit end marker */
		if ( option->tag == DHCP_END )
			break;
		/* Calculate length of this option.  Abort processing
		 * if the length is malformed (i.e. takes us beyond
		 * the end of the data block).
		 */
		option_len = dhcp_option_len ( option );
		remaining -= option_len;
		if ( remaining < 0 )
			break;
		/* Check for matching tag */
		if ( option->tag == tag )
			return option;
		/* Check for start of matching encapsulation block */
		if ( DHCP_ENCAPSULATOR ( tag ) &&
		     ( option->tag == DHCP_ENCAPSULATOR ( tag ) ) ) {
			/* Search within encapsulated option block */
			return find_dhcp_option_raw ( DHCP_ENCAPSULATED( tag ),
						      &option->data,
						      option->len );
		}
		option = ( ( ( void * ) option ) + option_len );
	}
	return NULL;
}

/**
 * Find DHCP option within all registered DHCP options blocks
 *
 * @v tag		DHCP option tag to search for
 * @ret option		DHCP option, or NULL if not found
 *
 * Searches within all registered DHCP option blocks for the specified
 * tag.  Encapsulated options may be searched for by using
 * DHCP_ENCAP_OPT() to construct the tag value.
 */
struct dhcp_option * find_dhcp_option ( unsigned int tag ) {
	struct dhcp_option_block *options;
	struct dhcp_option *option;

	list_for_each_entry ( options, &option_blocks, list ) {
		if ( ( option = find_dhcp_option_raw ( tag, options->data,
						       options->len ) ) )
			return option;
	}
	return NULL;
}

/**
 * Register DHCP option block
 *
 * @v options		DHCP option block
 *
 * Register a block of DHCP options
 */
void register_dhcp_options ( struct dhcp_option_block *options ) {
	list_add ( &options->list, &option_blocks );
}

/**
 * Unregister DHCP option block
 *
 * @v options		DHCP option block
 */
void unregister_dhcp_options ( struct dhcp_option_block *options ) {
	list_del ( &options->list );
}

/**
 * Allocate space for a block of DHCP options
 *
 * @v len		Maximum length of option block
 * @ret options		Option block, or NULL
 *
 * Creates a new DHCP option block and populates it with an empty
 * options list.  This call does not register the options block.
 */
struct dhcp_option_block * alloc_dhcp_options ( size_t len ) {
	struct dhcp_option_block *options;
	struct dhcp_option *option;

	options = malloc ( sizeof ( *options ) + len );
	if ( options ) {
		options->data = ( ( void * ) options + sizeof ( *options ) );
		options->len = len;
		if ( len ) {
			option = options->data;
			option->tag = DHCP_END;
		}
	}
	return options;
}

/**
 * Free DHCP options block
 *
 * @v options		Option block
 */
void free_dhcp_options ( struct dhcp_option_block *options ) {
	free ( options );
}