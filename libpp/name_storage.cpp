/**
 * @file name_storage.cpp
 * Storage of global names (filenames and symbols)
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 * @author John Levon
 */

#include <stdexcept>

#include "name_storage.h"
#include "demangle_symbol.h"
#include "file_manip.h"
#include "string_manip.h"

using namespace std;

image_name_storage image_names;
debug_name_storage debug_names;
symbol_name_storage symbol_names;


string const & image_name_storage::basename(image_name_id id) const
{
	stored_name const & n = get(id);
	if (n.name_processed.empty()) {
		n.name_processed = ::basename(n.name);
	}
	return n.name_processed;
}


string const & debug_name_storage::basename(debug_name_id id) const
{
	stored_name const & n = get(id);
	if (n.name_processed.empty()) {
		n.name_processed = ::basename(n.name);
	}
	return n.name_processed;
}


string const & symbol_name_storage::demangle(symbol_name_id id) const
{
	stored_name const & n = get(id);
	if (!n.name_processed.empty() || n.name.empty())
		return n.name_processed;

	if (n.name[0] != '?') {
		n.name_processed = demangle_symbol(n.name);
		return n.name_processed;
	}

	if (n.name.length() < 2 || n.name[1] != '?') {
		n.name_processed = "(no symbols)";
		return n.name_processed;
	}
	
	n.name_processed = "anonymous symbol from section ";
	n.name_processed += ltrim(n.name, "?");
	return n.name_processed;
}


bool operator<(debug_name_id const & lhs, debug_name_id const & rhs)
{
	// we compare by name rather by id since what user will see are
	// filename and when the criteria "samples count" give identical
	// result it's better to obtain result sorted by the user visible
	// property filename rather than by an obscure, invisible from user
	// point of view, file identifier property
	return debug_names.name(lhs) < debug_names.name(rhs);
}
