/**
 * @file profile_container.h
 * Container associating symbols and samples
 *
 * @remark Copyright 2002, 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 * @author John Levon
 */

#ifndef PROFILE_CONTAINER_H
#define PROFILE_CONTAINER_H

#include <string>
#include <vector>

#include "opp_symbol.h"
#include "utility.h"
#include "op_bfd.h"
#include "sample_container.h"
#include "format_flags.h"

/// a collection of sorted symbols
typedef std::vector<symbol_entry const *> symbol_collection;

class symbol_container;
class string_filter;
class profile_t;

/** store multiple samples files belonging to the same profiling session.
 * So on can hold samples files for arbitrary counter and binary image */
class profile_container : noncopyable {
public:
	/**
	 * Build an object to store information on samples. All parameters
	 * acts as hint for what you will request after recording samples and
	 * so on allow optimizations during recording the information.
	 *
	 * @param add_zero_samples_symbols Must we add to the symbol container
	 * symbols with zero samples count
	 *
	 * @param debug_info Optimize hint to add samples. If true line number
	 * are recorded. This boolean is a promise of what will be required as
	 * information in future. Avoiding line number lookup greatly improves
	 * performance.
	 *
	 * @param need_details true if we need to record all samples or to
	 * to record them at symbol level. This is an optimization hint
	 */
	profile_container(bool add_zero_samples_symbols, bool debug_info,
	                  bool need_details);

	~profile_container();
 
	/**
	 * add() -  record symbols/samples in the underlying container
	 *
	 * @param profile the samples files container
	 * @param abfd the associated bfd object
	 * @param app_name the owning application name of sample
	 *
	 * add() is an helper for delayed ctor. Take care you can't safely
	 * make any call to add after any other member function call.
	 * Obviously you can add only samples files which are coherent (same
	 * sampling rate, same events etc.)
	 */
	void add(profile_t const & profile, op_bfd const & abfd,
		 std::string const & app_name);

	/// Find a symbol from its image_name, vma, return zero if no symbol
	/// for this image at this vma
	symbol_entry const * find_symbol(std::string const & image_name,
					 bfd_vma vma) const;

	/// Find a symbol from its filename, linenr, return zero if no symbol
	/// at this location
	symbol_entry const * find_symbol(debug_name_id filename,
					size_t linenr) const;

	/// Find a sample by its symbol, vma, return zero if there is no sample
	/// at this vma
	sample_entry const * find_sample(symbol_entry const * symbol,
					 bfd_vma vma) const;

	/// used for select_symbols()
	struct symbol_choice {
		symbol_choice()
			: hints(cf_none), threshold(0.0), match_image(false) {}

		/// hints filled in
		column_flags hints;
		/// percentage threshold
		double threshold;
		/// match the image name only
		bool match_image;
		/// owning image name
		std::string image_name;
	};

	/**
	 * select_symbols - create a set of symbols sorted by sample count
	 * @param choice  parameters to use/fill in when selecting
	 */
	symbol_collection const select_symbols(symbol_choice & choice) const;

	/// Like select_symbols for filename without allowing sort by vma.
	std::vector<debug_name_id> const select_filename(double threshold) const;

	/// return the total number of samples
	unsigned int samples_count() const;

	/// Get the samples count which belongs to filename. Return 0 if
	/// no samples found.
	unsigned int samples_count(debug_name_id filename_id) const;
	/// Get the samples count which belongs to filename, linenr. Return
	/// 0 if no samples found.
	unsigned int samples_count(debug_name_id filename,
			   size_t linenr) const;

	/// return iterator to the first samples
	sample_container::samples_iterator begin() const;
	/// return iterator to the last samples
	sample_container::samples_iterator end() const;

	/// return iterator to the first samples for this symbol
	sample_container::samples_iterator begin(symbol_entry const *) const;
	/// return iterator to the last samples for this symbol
	sample_container::samples_iterator end(symbol_entry const *) const;

private:
	/// helper for add()
	void add_samples(profile_t const & profile,
	                 op_bfd const & abfd, symbol_index_t sym_index,
	                 u32 start, u32 end, bfd_vma base_vma,
	                 symbol_entry const * symbol);

	/**
	 * create an unique artificial symbol for an offset range. The range
	 * is only a hint of the maximum size of the created symbol. We
	 * give to the symbol an unique name as ?image_file_name#order and
	 * a range up to the nearest of syms or for the whole range if no
	 * syms exist after the start offset. the end parameter is updated
	 * to reflect the symbol range.
	 *
	 * The rationale here is to try to create symbols for alignment between
	 * function as little as possible and to create meaningfull symbols
	 * for special case such image w/o symbol.
	 */
	std::string create_artificial_symbol(op_bfd const & abfd, u32 start,
	                                     u32 & end, size_t & order);

	/// The symbols collected by oprofpp sorted by increased vma, provide
	/// also a sort order on samples count for each counter.
	scoped_ptr<symbol_container> symbols;
	/// The samples count collected by oprofpp sorted by increased vma,
	/// provide also a sort order on (filename, linenr)
	scoped_ptr<sample_container> samples;
	/// build() must count samples count for each counter so cache it here
	/// since user of profile_container often need it later.
	unsigned int total_count;

	/**
	 * Optimization hints for what information we are going to need,
	 * see the explanation in profile_container()	
	 */
	//@{
	bool debug_info;
	bool add_zero_samples_symbols;
	bool need_details;
	//@}
};

#endif /* !PROFILE_CONTAINER_H */