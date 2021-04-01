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

#include "commands/tools/wiki.hpp"
#include "options/global.hpp"
#include "tools/cli_setup.hpp"
#include "tools/references.hpp"

#include "genesis/utils/core/fs.hpp"
#include "genesis/utils/text/string.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// =================================================================================================
//      Setup
// =================================================================================================

void setup_wiki( CLI::App& app )
{
    // Create the options and subcommand objects.
    auto options = std::make_shared<WikiOptions>();
    auto sub = app.add_subcommand(
        "wiki",
        "Create wiki help pages for grenedalf."
    )->group( "" );

    // Need to capture the main app, as the wiki needs this to run.
    options->app = &app;
    while( options->app->get_parent() ) {
        options->app = options->app->get_parent();
    }

    // Markdown dir option.
    auto md_dir_opt = sub->add_option(
        "--md-dir",
        options->md_dir,
        "Directory with the Markdown files documenting the grenedalf commands."
    );
    md_dir_opt->group( "Settings" );
    md_dir_opt->check( CLI::ExistingDirectory );
    // md_dir_opt->required();

    // Out dir option.
    auto out_dir_opt = sub->add_option(
        "--out-dir",
        options->out_dir,
        "Directory to write Wiki files to. Should be a git clone of the wiki repository."
    );
    out_dir_opt->group( "Settings" );
    out_dir_opt->check( CLI::ExistingDirectory );
    // out_dir_opt->required();

    // Set the run function as callback to be called when this subcommand is issued.
    // Hand over the options by copy, so that their shared ptr stays alive in the lambda.
    sub->callback( [ options ]() {
        run_wiki( *options );
    });
}

// =================================================================================================
//      Helper Functions
// =================================================================================================

// -------------------------------------------------------------------------
//     App Subcommand Helpers
// -------------------------------------------------------------------------

/**
 * @brief Get the immediate subcommands of an App, sorted by name.
 */
std::vector<CLI::App const*> get_sorted_subcommands( CLI::App const* app )
{
    std::vector<CLI::App const*> subcomms;
    for( auto const& subcom : app->get_subcommands({}) ) {
        if( subcom->get_group() != "" ) {
            subcomms.push_back( subcom );
        }
    }

    std::sort(
        subcomms.begin(), subcomms.end(),
        []( CLI::App const* lhs, CLI::App const* rhs ){
            return lhs->get_name() < rhs->get_name();
        }
    );
    return subcomms;
}

/**
 * @brief Get all subcommands, recursively, of an App, sorted by name.
 */
std::vector<CLI::App const*> get_all_subcommands( CLI::App const* app )
{
    std::vector<CLI::App const*> result;

    // Fill with subcommands.
    auto subcomms = get_sorted_subcommands( app );
    for( auto const& sc : subcomms ) {
        result.push_back( sc );

        // Recurse. The copying is wasteful, but it's not that much, and I am lazy today.
        auto subsubcomms = get_all_subcommands( sc );
        for( auto const& ssc : subsubcomms ) {
            result.push_back( ssc );
        }
    }

    return result;
}

/**
 * @brief Add the contents of a file to a stream.
 */
void add_markdown_content( WikiOptions const& options, std::string const& md_file, std::ostream& os )
{
    using namespace genesis::utils;

    // Add markdown file content.
    std::string const fn = dir_normalize_path( options.md_dir ) + md_file + ".md";
    if( file_exists( fn ) ) {
        std::ifstream mds( fn );
        os << mds.rdbuf();
    } else {
        LOG_MSG << " - No documentation markdown found: " << md_file;
    }
}

// -------------------------------------------------------------------------
//     Make Options Table
// -------------------------------------------------------------------------

void make_options_table( CLI::App const& command, std::ostream& os )
{
    // Get the options that are part of this command.
    auto const options = command.get_options();

    // map from group name to table contents.
    // we use a vec to keep order.
    // std::map<std::string, std::string> opt_helps;
    std::vector<std::pair<std::string, std::string>> opt_helps;

    // Add lines for each group.
    for( auto const& opt : options ) {

        // Do not add help option.
        if( opt->get_name() == "-h,--help" || opt->get_name() == "--help" ) {
            continue;
        }

        // Write to temporary stream.
        std::stringstream tmp_os;

        // Simple version that makes a markdown table.
        // tmp_os << "| <nobr>`" << opt->get_name() << "`</nobr> ";
        // tmp_os << "|";
        // if( opt->get_required() ) {
        //     tmp_os << " **Required.**";
        // }
        // if( ! opt->help_aftername().empty() ) {
        //     // print stuff without leading space.
        //     tmp_os << " `" << opt->help_aftername().substr( 1 ) << "`<br>";
        // }
        //
        // auto descr = opt->get_description();
        // tmp_os << " " << descr << " |\n";
        // // tmp_os << " " << opt->get_description() << " |\n";
        // // tmp_os << "| " << opt->get_description() << " |\n";

        // Add content to the group help.
        tmp_os << "<tr><td><code>" << opt->get_name() << "</code></td>";
        tmp_os << "<td>";
        if( opt->get_required() ) {
            tmp_os << "<strong>Required.</strong>";
        }

        auto formatter = dynamic_cast<CLI::Formatter const*>( command.get_formatter().get() );
        auto opt_str = formatter->make_option_opts( opt );
        opt_str = genesis::utils::replace_all(
            opt_str,
            command.get_formatter()->get_label("REQUIRED"),
            ""
        );

        // Little special case: --threads defaults to the number of cores on the current system
        // where this wiki command is being run. Make this nicer.
        if( opt->get_name() == "--threads" ) {
            std::string tn;
            if( !opt->get_type_name().empty() ) {
                tn = formatter->get_label( opt->get_type_name() );
            }
            std::string search;
            if( !opt->get_default_str().empty() ) {
                search = tn + "=" + opt->get_default_str();
            }
            if( !search.empty() ) {
                opt_str = genesis::utils::replace_all( opt_str, search, tn );
            }
        }

        // Now print to the output.
        if( ! opt_str.empty() ) {
            tmp_os << " <code>" << genesis::utils::trim( opt_str ) << "</code><br>";
        }

        // Add description
        auto descr = opt->get_description();
        if( descr.substr( 0, 10 ) == "Required. " ) {
            descr = descr.substr( 10 );
        }
        tmp_os << " " << descr << "</td></tr>\n";
        // tmp_os << " " << opt->get_description() << " |\n";
        // tmp_os << "| " << opt->get_description() << " |\n";

        // Add content to the group help.
        // first check if the group was already used, and if not add it.
        auto get_group_content = [&]( std::string const& name ) -> std::string& {
            for( auto& elem : opt_helps ) {
                if( elem.first == name ) {
                    return elem.second;
                }
            }
            opt_helps.push_back({ name, "" });
            return opt_helps.back().second;
        };
        get_group_content( opt->get_group() ) += tmp_os.str();
        // opt_helps[ opt->get_group() ] += tmp_os.str();
    }

    // Simple markdown verison to print the groups and their tables.
    // for( auto const& gr : opt_helps ) {
    //     os << "**" << gr.first << ":**\n\n";
    //     os << "| Option  | Description |\n";
    //     os << "| ------- | ----------- |\n";
    //     // os << "| Option  | Type | Description |\n";
    //     // os << "| ------- | ---- | ----------- |\n";
    //     os << gr.second << "\n";
    // }

    // Print the groups and their tables
    os << "<table>\n";
    bool done_first_group = false;
    for( auto const& gr : opt_helps ) {
        if( done_first_group ) {
            os << "<tr height=30px></tr>\n";
        }
        os << "<thead><tr><th colspan=\"2\" align=\"left\">" << gr.first << "</th></tr></thead>\n";
        os << "<tbody>\n";
        os << gr.second;
        os << "</tbody>\n";
        done_first_group = true;
    }
    os << "</table>\n\n";
}

// -------------------------------------------------------------------------
//     Make Subcommands Table
// -------------------------------------------------------------------------

void make_subcommands_table( std::vector<CLI::App const*> subcomms, std::ostream& os )
{
    os << "| Subcommand  | Description |\n";
    os << "| ----------- | ----------- |\n";

    for( auto const& subcomm : subcomms ) {
        os << "| [" << subcomm->get_name() << "](../wiki/Subcommand:-" << subcomm->get_name() << ") ";
        os << "| " << subcomm->get_description() << " |\n";
    }
    os << "\n";
}

// -------------------------------------------------------------------------
//     Make Wiki Page
// -------------------------------------------------------------------------

void make_wiki_command_page( WikiOptions const& options, CLI::App const& command )
{
    using namespace genesis::utils;

    // User output.
    LOG_MSG << "Subcommand: " << command.get_name();

    // Get stuff of this command.
    auto const subcomms = command.get_subcommands({});

    // Open out file stream.
    std::string const out_file
        = dir_normalize_path( options.out_dir )
        + "Subcommand:-" + command.get_name() + ".md"
    ;
    if( ! file_exists( out_file )) {
        LOG_MSG << " - No existing wiki file!";
    }
    std::ofstream os( out_file );

    // Get the usage line.
    std::string usage = command.get_name();
    auto parent = const_cast< CLI::App& >( command ).get_parent();
    while( parent ) {
        usage  = parent->get_name() + " " + usage;
        parent = parent->get_parent();
    }

    // We do not count the help option, so we need to manually check if there are any others.
    bool has_options = false;
    for( auto const& opt : command.get_options() ) {
        if( opt->get_name() != "-h,--help" && opt->get_name() != "--help" ) {
            has_options = true;
            break;
        }
    }

    // Write command header.
    os << command.get_description() << "\n\n";
    os << "Usage: `" << usage;
    if( has_options ) {
        os << " [options]";
    }
    if( ! subcomms.empty() ) {
        if( command.get_require_subcommand_min() > 0 ) {
            os << " subcommand";
        } else {
            os << " [subcommand]";
        }
    }
    os << "`\n\n";

    // Print the options of the command.
    if( has_options ) {
        os << "## Options\n\n";
        make_options_table( command, os );
    }

    // Print the subcommands of this command.
    if( ! subcomms.empty() ) {
        os << "## Subommands\n\n";
        make_subcommands_table( subcomms, os );
    }

    // Add markdown file content.
    add_markdown_content( options, command.get_name(), os );

    // If there is a citation list for this command, add it in a nice format.
    if( citation_list.count( &command ) > 0 ) {
        os << "\n";
        os << "## Citation\n\n";
        os << "When using this method, please do not forget to cite\n\n";
        os << cite_markdown( citation_list[ &command ], true, false );
    }

    os.close();
}

// -------------------------------------------------------------------------
//     Make Wiki Home Page
// -------------------------------------------------------------------------

void make_wiki_home_page( WikiOptions const& options )
{
    using namespace genesis::utils;

    // Make Home page.
    LOG_MSG << "Home";

    // Open stream
    std::string const out_file = dir_normalize_path( options.out_dir ) + "Home.md";
    if( ! file_exists( out_file )) {
        LOG_MSG << " - No existing wiki file!";
    }
    std::ofstream os( out_file );

    // Add home header.
    add_markdown_content( options, "Home_header", os );
    os << "\n";

    // Add submodule lists.
    auto subcomms = get_sorted_subcommands( options.app );
    for( auto const& sc : subcomms ) {
        if( sc->get_group() == "" ) {
            continue;
        }

        os << "### Module `" << sc->get_name() << "`\n\n";
        os << sc->get_description() << "\n\n";
        make_subcommands_table( get_sorted_subcommands( sc ), os );
    }

    // Add home footer.
    add_markdown_content( options, "Home_footer", os );
    os.close();
}

// -------------------------------------------------------------------------
//     Side Bar
// -------------------------------------------------------------------------

void make_wiki_sidebar( WikiOptions const& options )
{
    using namespace genesis::utils;

    // Make Sidebar page.
    LOG_MSG << "Sidebar";

    // Open stream
    std::string const out_file = dir_normalize_path( options.out_dir ) + "_Sidebar.md";
    if( ! file_exists( out_file )) {
        LOG_MSG << " - No existing wiki file!";
    }
    std::ofstream os( out_file );

    // Add standard entries
    os << "[Home](../wiki)\n\n";
    os << "[General Usage](../wiki/General-Usage)\n\n";
    os << "[Phylogenetic Placement](../wiki/Phylogenetic-Placement)\n\n";

    // Add submodule lists.
    auto subcomms = get_sorted_subcommands( options.app );
    for( auto const& sc : subcomms ) {
        if( sc->get_group() == "" ) {
            continue;
        }

        os << "Module `" << sc->get_name() << "`\n\n";
        for( auto const& subcomm : get_sorted_subcommands( sc ) ) {
            os << " * [" << subcomm->get_name() << "](../wiki/Subcommand:-" << subcomm->get_name() << ")\n";
        }
        os << "\n";
    }

    os.close();
}

// =================================================================================================
//      Run
// =================================================================================================

void run_wiki( WikiOptions const& options )
{
    // Get all subcommands of the main app and make wiki pages for all subcommands.
    // auto subcomms = get_all_subcommands( options.app );
    // for( auto const& subcomm : subcomms ) {
    //     make_wiki_command_page( options, *subcomm );
    // }

    // Home page.
    make_wiki_home_page( options );
    make_wiki_sidebar( options );

    // Now, make pages for the commands of the modules.
    auto subcomms = get_sorted_subcommands( options.app );
    for( auto const& sc : subcomms ) {
        for( auto const& ssc : get_sorted_subcommands( sc )) {
            make_wiki_command_page( options, *ssc );
        }
    }
}
