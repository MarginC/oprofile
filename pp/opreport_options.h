/**
 * @file opreport_options.h
 * Options for opreport tool
 *
 * @remark Copyright 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 */

#ifndef OPREPORT_OPTIONS_H
#define OPREPORT_OPTIONS_H

#include <string>
#include <vector>
#include <iosfwd>

#include "common_option.h"
#include "utility.h"
#include "string_filter.h"
#include "symbol_sort.h"

class partition_files;
class merge_option;

namespace options {
	extern bool demangle;
	extern bool smart_demangle;
	extern bool symbols;
	extern bool debug_info;
	extern bool details;
	extern bool reverse_sort;
	extern bool exclude_dependent;
	extern sort_options sort_by;
	extern merge_option merge_by;
	extern bool global_percent;
	extern bool long_filenames;
	extern string_filter symbol_filter;
	extern double threshold;
	extern bool show_header;
	extern bool accumulated;
}

/**
 * a partition of sample filename to treat, each sub-list is a list of
 * sample to merge. filled by handle_options()
 */
extern scoped_ptr<partition_files> sample_file_partition;

/**
 * get_options - process command line
 * @param non_options vector of non options string
 *
 * Process the arguments, fatally complaining on error.
 */
void handle_options(std::vector<std::string> const & non_options);

#endif // OPREPORT_OPTIONS_H