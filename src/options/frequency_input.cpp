/*
    grenedalf - Genome Analyses of Differential Allele Frequencies
    Copyright (C) 2020-2021 Lucas Czech

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Contact:
    Lucas Czech <lczech@carnegiescience.edu>
    Department of Plant Biology, Carnegie Institution For Science
    260 Panama Street, Stanford, CA 94305, USA
*/

#include "options/frequency_input.hpp"

#include "options/global.hpp"
#include "tools/misc.hpp"

#include "genesis/population/formats/simple_pileup_input_iterator.hpp"
#include "genesis/population/formats/simple_pileup_reader.hpp"
#include "genesis/population/formats/sync_input_iterator.hpp"
#include "genesis/population/formats/sync_reader.hpp"
#include "genesis/population/formats/vcf_input_iterator.hpp"
#include "genesis/population/functions/genome_region.hpp"
#include "genesis/population/functions/variant.hpp"
#include "genesis/population/genome_region.hpp"
#include "genesis/utils/containers/filter_iterator.hpp"
#include "genesis/utils/core/fs.hpp"
#include "genesis/utils/text/string.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>

// =================================================================================================
//      Setup Functions
// =================================================================================================

// -------------------------------------------------------------------------
//     All Input File Types
// -------------------------------------------------------------------------

void FrequencyInputOptions::add_frequency_input_opts_to_app(
    CLI::App* sub,
    // bool required,
    bool with_sample_name_prefix,
    bool with_filters,
    std::string const& group
) {
    // (void) required;

    // Add input file type options.
    add_pileup_input_opt_to_app( sub, false, group );
    add_sync_input_opt_to_app( sub, false, group );
    add_vcf_input_opt_to_app( sub, false, group );

    // Only one input file format allowed at a time.
    pileup_file_.option->excludes( sync_file_.option );
    pileup_file_.option->excludes( vcf_file_.option );
    sync_file_.option->excludes( pileup_file_.option );
    sync_file_.option->excludes( vcf_file_.option );
    vcf_file_.option->excludes( pileup_file_.option );
    vcf_file_.option->excludes( sync_file_.option );

    // Additional options.
    if( with_sample_name_prefix ) {
        add_sample_name_prefix_opt_to_app( sub, group );
    }
    if( with_filters ) {
        add_filter_opts_to_app( sub, group );
    }
}

// -------------------------------------------------------------------------
//     Individual File Types
// -------------------------------------------------------------------------

CLI::Option* FrequencyInputOptions::add_pileup_input_opt_to_app(
    CLI::App* sub,
    bool required,
    std::string const& group
) {
    // Correct setup check.
    internal_check(
        pileup_file_.option == nullptr,
        "Cannot use the same FrequencyInputOptions object multiple times."
    );

    // TODO add options for reading: with quality, with ancestral base, quality encoding

    // Add the option
    pileup_file_.option = sub->add_option(
        "--pileup-file",
        pileup_file_.value,
        "Path to an (m)pileup file."
    );
    pileup_file_.option->check( CLI::ExistingFile );
    pileup_file_.option->group( group );
    if( required ) {
        pileup_file_.option->required();
    }

    return pileup_file_.option;
}

CLI::Option* FrequencyInputOptions::add_sync_input_opt_to_app(
    CLI::App* sub,
    bool required,
    std::string const& group
) {
    // Correct setup check.
    internal_check(
        sync_file_.option == nullptr,
        "Cannot use the same FrequencyInputOptions object multiple times."
    );

    // Add the option
    sync_file_.option = sub->add_option(
        "--sync-file",
        sync_file_.value,
        "Path to a sync file, as specified by PoPoolation2."
    );
    sync_file_.option->check( CLI::ExistingFile );
    sync_file_.option->group( group );
    if( required ) {
        sync_file_.option->required();
    }

    return sync_file_.option;
}

CLI::Option* FrequencyInputOptions::add_vcf_input_opt_to_app(
    CLI::App* sub,
    bool required,
    std::string const& group
) {
    // Correct setup check.
    internal_check(
        vcf_file_.option == nullptr,
        "Cannot use the same FrequencyInputOptions object multiple times."
    );

    // Add the option
    vcf_file_.option = sub->add_option(
        "--vcf-file",
        vcf_file_.value,
        "Path to a VCF file."
    );
    vcf_file_.option->check( CLI::ExistingFile );
    vcf_file_.option->group( group );
    if( required ) {
        vcf_file_.option->required();
    }

    return vcf_file_.option;
}

// -------------------------------------------------------------------------
//     Additional Input Options
// -------------------------------------------------------------------------

CLI::Option* FrequencyInputOptions::add_sample_name_prefix_opt_to_app(
    CLI::App* sub,
    std::string const& group
) {
    // Correct setup check.
    internal_check(
        sample_name_prefix_.option == nullptr,
        "Cannot use the same FrequencyInputOptions object multiple times."
    );

    // Pileup does not have sample names, so offer a prefix option for this.
    sample_name_prefix_.option = sub->add_option(
        "--sample-name-prefix",
        sample_name_prefix_.value,
        "Some file types do not contain sample names, such as (m)pileup or sync files. For such "
        "file types, this prefix followed by indices 1..n is used instead to provide unique names "
        "per sample that we use in the output and the `--filter-samples-include` and "
        "`--filter-samples-exclude` options. You can for example use \"Sample_\" as a prefix. "
        "If not provided, we simply use numbers 1..n as sample names for these files."
    );
    sample_name_prefix_.option->group( group );
    // sample_name_prefix_.option->needs( pileup_file_.option );

    return sample_name_prefix_.option;
}

void FrequencyInputOptions::add_filter_opts_to_app(
    CLI::App* sub,
    std::string const& group
) {
    // Correct setup check.
    internal_check(
        filter_region_.option == nullptr,
        "Cannot use the same FrequencyInputOptions object multiple times."
    );

    // Add option for genomic region filter.
    filter_region_.option = sub->add_option(
        "--filter-region",
        filter_region_.value,
        "Genomic region to filter for, in the format \"chr\", \"chr:position\", \"chr:start-end\", "
        "or \"chr:start..end\". If not provided, the whole input file is used."
    );
    filter_region_.option->group( group );

    // Add option for sample name filter.
    filter_samples_include_.option = sub->add_option(
        "--filter-samples-include",
        filter_samples_include_.value,
        "Sample names to include (all other samples are excluded); either a comma- or tab-separated "
        " list, or a file with one sample name per line. If no sample filter is provided, all "
        "samples in the input file are used."
    );
    filter_samples_include_.option->group( group );

    // And the other way round.
    filter_samples_exclude_.option = sub->add_option(
        "--filter-samples-exclude",
        filter_samples_exclude_.value,
        "Sample names to exclude (all other samples are included); either a comma- or tab-separated "
        " list, or a file with one sample name per line. If no sample filter is provided, all "
        "samples in the input file are used."
    );
    filter_samples_exclude_.option->group( group );
    filter_samples_exclude_.option->excludes( filter_samples_include_.option );
}

// -------------------------------------------------------------------------
//     Window Options
// -------------------------------------------------------------------------

void FrequencyInputOptions::add_sliding_window_opts_to_app(
    CLI::App* sub,
    std::string const& group
) {
    // Width
    window_width_.option = sub->add_option(
        "--window-width",
        window_width_.value,
        "Width of each window along the chromosome."
    );
    window_width_.option->group( group );

    // Stride
    window_stride_.option = sub->add_option(
        "--window-stride",
        window_stride_.value,
        "Stride between windows along the chromosome, that is how far to move to get to the next "
        "window. If set to 0 (default), this is set to the same value as the `--window-width`."
    );
    window_stride_.option->group( group );
}

// =================================================================================================
//      Run Functions
// =================================================================================================

// -------------------------------------------------------------------------
//     sample_names
// -------------------------------------------------------------------------

std::vector<std::string> const& FrequencyInputOptions::sample_names() const
{
    prepare_data_();
    return sample_names_;
}

// -------------------------------------------------------------------------
//     get_iterator
// -------------------------------------------------------------------------

genesis::utils::Range<genesis::utils::LambdaIterator<genesis::population::Variant>>
FrequencyInputOptions::get_iterator() const
{
    // Return the range of the data that we just captured and converted.
    prepare_data_();
    return { generator_.begin(), generator_.end() };
}

// -------------------------------------------------------------------------
//     get_base_count_sliding_window_iterator
// -------------------------------------------------------------------------

genesis::population::SlidingWindowIterator<
    genesis::utils::LambdaIterator<genesis::population::Variant>,
    genesis::population::Variant,
    std::vector<genesis::population::BaseCounts>
>
FrequencyInputOptions::get_base_count_sliding_window_iterator() const
{
    using namespace genesis;
    using namespace genesis::population;

    // User-provided sliding window settings
    SlidingWindowIteratorSettings<Variant, std::vector<BaseCounts>> settings;
    settings.width = window_width_.value;
    settings.stride = window_stride_.value;

    // Conversion functions for the sliding window iterator.
    settings.entry_input_function = []( Variant const& variant ) -> std::vector<BaseCounts> const& {
        // TODO move instead?!
        return variant.samples;
    };
    settings.chromosome_function = []( Variant const& variant ) -> std::string const& {
        return variant.chromosome;
    };
    settings.position_function = []( Variant const& variant ){
        return variant.position;
    };

    // Make sure that we have the iterator over the input file set up, and then return the
    // window iterator.
    prepare_data_();
    return make_sliding_window_iterator( settings, generator_.begin(), generator_.end() );
}

// -------------------------------------------------------------------------
//     get_variant_sliding_window_iterator
// -------------------------------------------------------------------------

genesis::population::SlidingWindowIterator<
    genesis::utils::LambdaIterator<genesis::population::Variant>,
    genesis::population::Variant
>
FrequencyInputOptions::get_variant_sliding_window_iterator() const
{
    using namespace genesis;
    using namespace genesis::population;

    // User-provided sliding window settings
    SlidingWindowIteratorSettings<Variant> settings;
    settings.width = window_width_.value;
    settings.stride = window_stride_.value;

    // Conversion functions for the sliding window iterator.
    settings.entry_input_function = []( Variant const& variant ) -> Variant const& {
        // TODO move instead?!
        return variant;
    };
    settings.chromosome_function = []( Variant const& variant ) -> std::string const& {
        return variant.chromosome;
    };
    settings.position_function = []( Variant const& variant ){
        return variant.position;
    };

    // Make sure that we have the iterator over the input file set up, and then return the
    // window iterator.
    prepare_data_();
    return make_sliding_window_iterator( settings, generator_.begin(), generator_.end() );
}

// =================================================================================================
//      Internal Helpers
// =================================================================================================

// -------------------------------------------------------------------------
//     prepare_data_
// -------------------------------------------------------------------------

void FrequencyInputOptions::prepare_data_() const
{
    using namespace genesis;
    using namespace genesis::population;
    using namespace genesis::utils;

    // Checks for internal correct setup
    if( static_cast<bool>( generator_ ) || !sample_names_.empty() ) {
        // Nothing to be done. We already prepared the data.
        return;
    }

    // Check that we have exactly one input file type.
    // We use size_t even though it's a bool, to be able to quickly check that.
    auto const is_pileup = static_cast<size_t>( pileup_file_.option && *pileup_file_.option );
    auto const is_sync   = static_cast<size_t>( sync_file_.option   && *sync_file_.option );
    auto const is_vcf    = static_cast<size_t>( vcf_file_.option    && *vcf_file_.option );
    if( is_pileup + is_sync + is_vcf != 1 ) {
        throw CLI::ValidationError(
            "Exactly one input file of one type has to be provided."
        );
    }

    // If a sample name prefix is given, we check that this is only for the allowed file types.
    if( sample_name_prefix_.option && *sample_name_prefix_.option  ) {
        if( ! is_pileup && ! is_sync ) {
            throw CLI::ValidationError(
                "Can only use " + sample_name_prefix_.option->get_name()  + " for input file "
                "formats that do not already have sample sames, such as (m)pileup or sync files."
            );
        }
    }

    // Here, we need to select the different input sources and transform them into a uniform
    // iterator, using lambdas with std::function for type erasure. Additionally, we want region
    // filtering. Ideally, we would want to apply that afterwards, but that would just introduce
    // yet another level of indirection, as we'd need another lambda iterator to achieve that.
    // Sometimes, static types suck... Well, so instead, we apply the filter beforehand (which has
    // the additonal advantage that we do not need to convert irrelevant positions), but which
    // comes with the need to have individual filter iterators for each input data source,
    // meaning there is some incidental code duplication (or at least, very similar code blocks)
    // in the functions below... :-(

    if( pileup_file_.option && *pileup_file_.option ) {
        prepare_data_pileup_();
    }
    if( sync_file_.option && *sync_file_.option ) {
        prepare_data_sync_();
    }
    if( vcf_file_.option && *vcf_file_.option ) {
        prepare_data_vcf_();
    }
}

// -------------------------------------------------------------------------
//     prepare_data_pileup_
// -------------------------------------------------------------------------

void FrequencyInputOptions::prepare_data_pileup_() const
{
    using namespace genesis;
    using namespace genesis::population;
    using namespace genesis::utils;

    // Assert that this function is only called in a context where the data is not yet prepared.
    internal_check(
        ! static_cast<bool>( generator_ ) && sample_names_.empty(),
        "prepare_data_pileup_() called in an invalid context."
    );

    // Prepare the base Reader. Currently not really needed, as we use default settings anyway,
    // but we will want to adjust that in the future to accomodate things like quality encoding
    // and different columns in the pileup file.
    // TODO add pileup settings!
    auto reader = SimplePileupReader();
    // reader.quality_encoding( genesis::sequence::QualityEncoding::kIllumina13 );

    // Open the file, which aleady reads the first line. We use this to get the number of
    // samples in the pileup, and create dummy names for them.
    // We might later open it again to incorporate the sample name filtering, because to do that,
    // we first need to know how many samples there are in total...
    // Maybe there is a smart way to avoid that, but for now, that seems easiest.
    auto it = SimplePileupInputIterator( utils::from_file( pileup_file_.value ), reader );
    if( ! it ) {
        throw CLI::ValidationError(
            pileup_file_.option->get_name() + "(" +
            pileup_file_.value + ")",
            "Invalid empty input (m)pileup file."
        );
    }
    auto const smp_cnt = it->samples.size();
    for( size_t i = 0; i < smp_cnt; ++i ) {
        sample_names_.push_back( sample_name_prefix_.value + std::to_string(i+1) );
    }

    // Filter sample names as needed. This is a bit cumbersome, but gets the job done.
    if( ! filter_samples_include_.value.empty() || ! filter_samples_exclude_.value.empty() ) {
        // Not both can be given at the same time, as we made the options mutually exclusive.
        assert( filter_samples_include_.value.empty() != filter_samples_exclude_.value.empty() );

        // Get the filter, as bool (which samples to use), and as indices of these samples.
        auto const sample_filter  = get_sample_filter( sample_names_ );
        auto const sample_indices = get_sample_filter_indices( sample_filter );

        // Now restart the iteration, this time with the filtering, and renew the sample names.
        // We simply do an internal check to verify the file - we checked already above when
        // opening it for the first time, so it should be okay now as well.
        it = SimplePileupInputIterator(
            utils::from_file( pileup_file_.value ), sample_filter, reader
        );
        internal_check( it.good(), "Pileup file became invalid." );
        sample_names_.clear();
        for( auto idx : sample_indices ) {
            sample_names_.push_back( sample_name_prefix_.value + std::to_string(idx+1) );
        }
    }

    // Apply region filter if necessary.
    if( filter_region_.value.empty() ) {
        // Create a generator that reads pileup.
        generator_ = LambdaIteratorGenerator<Variant>(
            [it]() mutable -> std::shared_ptr<Variant>{
                if( it ) {
                    auto res = std::make_shared<Variant>( convert_to_variant(*it) );
                    ++it;
                    return res;
                } else {
                    return nullptr;
                }
            }
        );
    } else {
        auto const region = parse_genome_region( filter_region_.value );
        auto region_filtered_range = make_filter_range(
            [region]( SimplePileupReader::Record const& record ){
                return genesis::population::is_covered( region, record );
            },
            // Use the iterator and a default constructed dummy as begin and end.
            it, SimplePileupInputIterator()
        );

        // Create a generator that reads pileup and filters by region.
        auto beg = region_filtered_range.begin();
        auto end = region_filtered_range.end();
        generator_ = LambdaIteratorGenerator<Variant>(
            [beg, end]() mutable -> std::shared_ptr<Variant>{
                if( beg != end ) {
                    auto res = std::make_shared<Variant>( convert_to_variant( *beg ));
                    ++beg;
                    return res;
                } else {
                    return nullptr;
                }
            }
        );
    }
}

// -------------------------------------------------------------------------
//     prepare_data_sync_
// -------------------------------------------------------------------------

void FrequencyInputOptions::prepare_data_sync_() const
{
    // We here follow the same approach as above in prepare_data_pileup_(). See there for details.
    // TODO there is a lot if similar and duplicate code in here! ugly! refactor at some point!

    using namespace genesis;
    using namespace genesis::population;
    using namespace genesis::utils;

    // Assert that this function is only called in a context where the data is not yet prepared.
    internal_check(
        ! static_cast<bool>( generator_ ) && sample_names_.empty(),
        "prepare_data_sync_() called in an invalid context."
    );

    // Open the file, which aleady reads the first line. We use this to get the number of
    // samples in the sycn file, and create dummy names for them.
    // We might later open it again to incorporate the sample name filtering, because to do that,
    // we first need to know how many samples there are in total...
    // Maybe there is a smart way to avoid that, but for now, that seems easiest.
    auto it = SyncInputIterator( from_file( sync_file_.value ));
    if( ! it ) {
        throw CLI::ValidationError(
            sync_file_.option->get_name() + "(" +
            sync_file_.value + ")",
            "Invalid empty input sync file."
        );
    }
    auto const smp_cnt = it->samples.size();
    for( size_t i = 0; i < smp_cnt; ++i ) {
        sample_names_.push_back( sample_name_prefix_.value + std::to_string(i+1) );
    }

    // Filter sample names as needed. This is a bit cumbersome, but gets the job done.
    if( ! filter_samples_include_.value.empty() || ! filter_samples_exclude_.value.empty() ) {
        // Not both can be given at the same time, as we made the options mutually exclusive.
        assert( filter_samples_include_.value.empty() != filter_samples_exclude_.value.empty() );

        // Get the filter, as bool (which samples to use), and as indices of these samples.
        auto const sample_filter  = get_sample_filter( sample_names_ );
        auto const sample_indices = get_sample_filter_indices( sample_filter );

        // Now restart the iteration, this time with the filtering, and renew the sample names.
        // We simply do an internal check to verify the file - we checked already above when
        // opening it for the first time, so it should be okay now as well.
        it = SyncInputIterator( from_file( sync_file_.value ), sample_filter );
        internal_check( it.good(), "Sync file became invalid." );
        sample_names_.clear();
        for( auto idx : sample_indices ) {
            sample_names_.push_back( sample_name_prefix_.value + std::to_string(idx+1) );
        }
    }

    // Apply region filter if necessary.
    if( filter_region_.value.empty() ) {
        // Create a generator that reads pileup.
        generator_ = LambdaIteratorGenerator<Variant>(
            [it]() mutable -> std::shared_ptr<Variant>{
                if( it ) {
                    auto res = std::make_shared<Variant>( *it );
                    ++it;
                    return res;
                } else {
                    return nullptr;
                }
            }
        );
    } else {
        auto const region = parse_genome_region( filter_region_.value );
        auto region_filtered_range = make_filter_range(
            [region]( Variant const& variant ){
                return genesis::population::is_covered( region, variant );
            },
            // Use the iterator and a default constructed dummy as begin and end.
            it, SyncInputIterator()
        );

        // Create a generator that reads pileup and filters by region.
        auto beg = region_filtered_range.begin();
        auto end = region_filtered_range.end();
        generator_ = LambdaIteratorGenerator<Variant>(
            [beg, end]() mutable -> std::shared_ptr<Variant>{
                if( beg != end ) {
                    auto res = std::make_shared<Variant>( *beg );
                    ++beg;
                    return res;
                } else {
                    return nullptr;
                }
            }
        );
    }
}

// -------------------------------------------------------------------------
//     prepare_data_vcf_
// -------------------------------------------------------------------------

void FrequencyInputOptions::prepare_data_vcf_() const
{
    using namespace genesis;
    using namespace genesis::population;
    using namespace genesis::utils;

    // Assert that this function is only called in a context where the data is not yet prepared.
    internal_check(
        ! static_cast<bool>( generator_ ) && sample_names_.empty(),
        "prepare_data_vcf_() called in an invalid context."
    );

    // TODO buffer size

    // Prepare the base iterator.
    // See if we want to filter by sample name, and if so, resolve the name list.
    auto vcf_in = VcfInputIterator();
    if( ! filter_samples_include_.value.empty() ) {
        auto const list = get_sample_name_list( filter_samples_include_.value );
        vcf_in = VcfInputIterator( vcf_file_.value, list );
    } else if( ! filter_samples_exclude_.value.empty() ) {
        auto const list = get_sample_name_list( filter_samples_exclude_.value );
        vcf_in = VcfInputIterator( vcf_file_.value, list, true );
    } else {
        vcf_in = VcfInputIterator( vcf_file_.value );
    }
    if( !vcf_in.header().has_format("AD") ) {
        throw std::runtime_error(
            "Cannot use VCF input file that does not have the `AD` format field."
        );
    }

    // Get the sample names. This will only contain the filtered names.
    // Then, create a filter over the input that only allows biallelic SNPs with the AD format field.
    // Everything else we cannot use anyway for our subsequent conversion steps.
    sample_names_ = vcf_in.header().get_sample_names();
    auto vcf_range = utils::make_filter_range([]( VcfRecord const& record ){
        bool const is_snp = record.is_snp() && record.get_alternatives_count() == 1;
        bool const has_ad = record.has_format( "AD" );
        return is_snp && has_ad;
    }, vcf_in, {} );

    // Apply region filter if necessary.
    if( filter_region_.value.empty() ) {
        // Need variables that can be captures by copy...
        auto beg = vcf_range.begin();
        auto end = vcf_range.end();

        // Create an iterator that erases the type (which here is a complex templated type with
        // the VcfInputIterator and FilterIterator and all that). We use a lambda capture by
        // mutable value, so that the actual iterator is stored (as a copy, but that works) in the
        // lambda capture even after this whole function here is executed. That is, the generator_
        // keeps the underlying VCF iterator alive via the lambda capture. Wow.
        // Addendum: There was a nasty bug hidden in the implementation of VcfInputIterator,
        // as it uses a thread pool to pre-parse Vcf Records, but that thread pool was storing
        // invalid pointers after a race condition where the above setup deletes the original
        // copy of the iterator (`vcf_in`), but the thread would still want to use it...
        // That took a while to figure out, and is fixed now by having the thread pool keep copies
        // of the internal members of VcfFormatIterator of its own.
        generator_ = LambdaIteratorGenerator<Variant>(
            [beg, end]() mutable -> std::shared_ptr<Variant>{
                if( beg != end ) {
                    auto res = std::make_shared<Variant>( convert_to_variant(*beg) );
                    ++beg;
                    return res;
                } else {
                    return nullptr;
                }
            }
        );
    } else {
        auto const region = parse_genome_region( filter_region_.value );
        auto region_filtered_range = make_filter_range(
            [region]( VcfRecord const& record ){
                return genesis::population::is_covered( region, record );
            },
            vcf_range.begin(), vcf_range.end()
        );

        // Create a generator that reads pileup and filters by region.
        auto beg = region_filtered_range.begin();
        auto end = region_filtered_range.end();
        generator_ = LambdaIteratorGenerator<Variant>(
            [beg, end]() mutable -> std::shared_ptr<Variant>{
                if( beg != end ) {
                    auto res = std::make_shared<Variant>( convert_to_variant(*beg) );
                    ++beg;
                    return res;
                } else {
                    return nullptr;
                }
            }
        );
    }
}

// -------------------------------------------------------------------------
//     Sample Name Filtering
// -------------------------------------------------------------------------

std::vector<std::string> FrequencyInputOptions::get_sample_name_list( std::string const& value ) const
{
    using namespace genesis::utils;

    // If the input is a file, read it line by line as sample names. Otherwise, split by comma.
    if( is_file( value ) ) {
        return file_read_lines( value );
    } else {
        return split( value, ",\t" );
    }
}

std::vector<bool> FrequencyInputOptions::get_sample_filter(
    std::vector<std::string> const& sample_names
) const {

    // Get whether we want to include or exclude sample names.
    bool const is_include = ! filter_samples_include_.value.empty();
    bool const is_exclude = ! filter_samples_exclude_.value.empty();

    // This function is only called when we actually have sample name filters given by the user.
    // Also, Not both can be given at the same time, as we made the options mutually exclusive.
    internal_check( is_include || is_exclude, "get_sample_filter() with no sample names" );
    internal_check( is_include != is_exclude, "get_sample_filter() with include and exclude" );

    // Get the sample names, depending on which type (inc/exc) we have.
    auto const list = get_sample_name_list(
        is_include ? filter_samples_include_.value : filter_samples_exclude_.value
    );

    // Prepare a filter vector and go through all provided sample names, and set the filter.
    // We init the filter so that in the include case, all are false first, and in the exclude
    // case, all are true first. Then, we can set the listed indices accordingly.
    auto sample_filter = std::vector<bool>( sample_names.size(), is_exclude );
    for( auto const& sn : list ) {
        auto it = std::find( sample_names.begin(), sample_names.end(), sn );
        if( it == sample_names.end() ) {
            throw CLI::ValidationError(
                "Invalid sample name used for filtering: \"" + sn  + "\"."
            );
        } else {
            auto const index = it - sample_names.begin();
            sample_filter[ index ] = is_include;
        }
    }

    return sample_filter;
}

std::vector<size_t> FrequencyInputOptions::get_sample_filter_indices(
    std::vector<bool> const& sample_filter
) const {
    std::vector<size_t> sample_indices;
    for( size_t i = 0; i < sample_filter.size(); ++i ) {
        if( sample_filter[i] ) {
            sample_indices.push_back(i);
        }
    }
    return sample_indices;
}
