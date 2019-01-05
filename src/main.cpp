// Include-Gardener
//
// Copyright (C) 2017  Christian Haettich [feddischson]
//
// This program is free software; you can redistribute it
// and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation;
// either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General
// Public License along with this program; if not, see
// <http://www.gnu.org/licenses/>.
//
#include <fstream>

#include <boost/graph/graphml.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>
#include <boost/property_map/transform_value_property_map.hpp>

#include "config.h"
#include "detector.h"
#include "file_detector.h"
#include "parser.h"

using namespace INCLUDE_GARDENER;
using namespace std;
namespace po = boost::program_options;

static const std::string GARDENER_VERSION = _GARDENER_VERSION;

int init_options(int argc, char* argv[]);
void print_options(void);

// Note: use "dot -Tsvg graph.dot > graph.svg" to create svg.

struct {
   int no_threads;
   int recursive_limit;
   string language;
   string config_path;
   std::string format;
   vector<string> include_paths;
   vector<string> process_paths;
   std::vector<string> exclude;
   std::string out_file;
} opts;

int main(int argc, char* argv[]) {
   Graph g;
   Config::Ptr config = nullptr;

   int ret_val = init_options(argc, argv);
   if (ret_val != 0) {
      return ret_val;
   }
   print_options();

   config = Config::get_cfg(opts.config_path);

   if (!config->supports_language(opts.language)) {
      cerr << "Error: Language " << opts.language << " not supported." << endl;
      return -1;
   }
   BOOST_LOG_TRIVIAL(trace) << *config;

   File_Detector detector =
       File_Detector(config->get_file_detection(opts.language), opts.exclude,
                     opts.process_paths, opts.recursive_limit);

   detector.get();

   return 0;
}

int init_options(int argc, char* argv[]) {
   opts.no_threads = 2;
   opts.recursive_limit = -1;
   opts.language = "c";
   opts.config_path = _GARDENER_CONFIG_PATH;
   opts.format = "dot";
   opts.out_file = "";

   //
   // use boost's command line parser
   //
   po::options_description desc("Options");
   desc.add_options()("help,h", "displays this help message and exit")(
       "version,v", "displays version information")("verbose,V",
                                                    "sets verbosity")(
       "include-path,I", po::value<vector<string> >()->composing(),
       "include path")("out-file,o", po::value<string>(), "output file")(
       "format,f", po::value<string>(),
       "output format (suported formats: dot, xml/graphml)")(
       "process-path,P", po::value<vector<string> >()->composing(),
       "path which is processed")(
       "exclude,e", po::value<vector<string> >()->composing(),
       "regular expressions to exclude specific files")(
       "recursive-limit,L", po::value<int>(),
       "limits recursive processing (default=-1 = unlimited)")(
       "threads,j", po::value<int>(),
       "defines number of worker threads (default=2)")(
       "language,l", po::value<string>(), "selects the language (default=c)")(
       "config,c", po::value<string>(),
       "path to the config file (default=gardener.conf)");
   po::positional_options_description pos;
   pos.add("process-path", -1);

   // the process-path arguments can also be provided als post-arguments
   po::variables_map vm;
   try {
      po::store(po::command_line_parser(argc, argv)
                    .options(desc)
                    .positional(pos)
                    .run(),
                vm);
      po::notify(vm);
   } catch (boost::program_options::unknown_option& e) {
      cerr << e.what() << endl;
      return -1;
   }

   // print help if required
   if (true == vm.count("help")) {
      cout << desc << endl;
      return 1;
   }

   if (true == vm.count("version")) {
      cout << "Include Gardener Version " << GARDENER_VERSION << endl;
      return -1;
   }

   // ensure, that at least one process path is provided
   if (false == vm.count("process-path")) {
      cerr << "No input provided!" << endl << endl << desc << endl;
      return -1;
   }

   // Sets log level to warning if verbose is not set.
   // This must be done bevore useing any BOOST_LOG_TRIVIAL statement.
   //
   if (false == vm.count("verbose")) {
      boost::log::core::get()->set_filter(boost::log::trivial::severity >=
                                          boost::log::trivial::warning);
   }

   if (true == vm.count("exclude")) {
      opts.exclude = vm["exclude"].as<vector<string> >();
   }

   if (true == vm.count("config")) {
      opts.config_path = vm["config"].as<string>();
   }

   if (!boost::filesystem::exists(opts.config_path)) {
      cerr << "Error: config file " << opts.config_path << " not found."
           << endl;
      return -1;
   }

   if (true == vm.count("language")) {
      opts.language = vm["language"].as<string>();
      std::transform(opts.language.begin(), opts.language.end(),
                     opts.language.begin(), ::tolower);
   }

   // extract the format
   // currently, only the dot format is supported
   if (true == vm.count("format")) {
      opts.format = vm["format"].as<string>();
   }

   if (true == vm.count("threads")) {
      opts.no_threads = vm["threads"].as<int>();
      if (opts.no_threads == 0) {
         cerr << "Error: Number of threads is set to 0, which is not allowed."
              << endl
              << "Please use at least one worker thread." << endl;
         return -1;
      }
   }

   if (true == vm.count("recursive-limit")) {
      opts.recursive_limit = vm["recursive-limit"].as<int>();
   }

   if (!("" == opts.format || "dot" == opts.format || "xml" == opts.format ||
         "graphml" == opts.format)) {
      cerr << "Unrecognized format: " << opts.format << endl
           << endl
           << desc << endl;
      return -1;
   }

   if (true == vm.count("include-path")) {
      opts.include_paths = vm["include-path"].as<vector<string> >();
   }
   opts.process_paths = vm["process-path"].as<vector<string> >();

   if (true == vm.count("out-file")) {
      opts.out_file = vm["out-file"].as<string>();
   }

   return 0;
}

void print_options(void) {
   BOOST_LOG_TRIVIAL(trace) << "no_threads:      " << opts.no_threads;
   BOOST_LOG_TRIVIAL(trace) << "recursive_limit: " << opts.recursive_limit;
   BOOST_LOG_TRIVIAL(trace) << "language:        " << opts.language;
   BOOST_LOG_TRIVIAL(trace) << "config_path:     " << opts.config_path;
   BOOST_LOG_TRIVIAL(trace) << "format:          " << opts.format;
   BOOST_LOG_TRIVIAL(trace) << "out_file:        " << opts.out_file;
   BOOST_LOG_TRIVIAL(trace) << "include_paths:   ";
   for (auto p : opts.include_paths) {
      BOOST_LOG_TRIVIAL(trace) << "    " << p;
   }
   BOOST_LOG_TRIVIAL(trace) << "process_paths:   ";
   for (auto p : opts.process_paths) {
      BOOST_LOG_TRIVIAL(trace) << "    " << p;
   }
   BOOST_LOG_TRIVIAL(trace) << "exclude:         ";
   for (auto e : opts.exclude) {
      BOOST_LOG_TRIVIAL(trace) << "    " << e;
   }
}

// vim: filetype=cpp et ts=3 sw=3 sts=3
