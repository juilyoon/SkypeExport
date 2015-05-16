#include "model/skypeparser.h"
#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <iostream>

int main( int argc, char **argv )
{
	// FIXME: command line parameters that would be useful to add:
	// -l 0 / --ltime 0 (defaults to 1, but if set to 0 it changes all localtime() calls to gmtime())
	// -24h 1 / --24htime 1 (defaults to 0, but if set to 1 it changes the behavior of the time-of-day output to use 24h time; do this by changing all formatTime(1) calls to formatTime(2))

	// prepare command line parameters
	po::options_description desc( "Available options" );
	desc.add_options()
		( "help,h", "show this help message" )
		( "db,i", po::value<std::string>()->default_value("./main.db"), "path to your Skype profile's main.db" )
		( "outpath,o", po::value<std::string>()->default_value("./ExportedHistory"), "path where all html files will be written; will be created if missing" )
		( "contacts,c", po::value<std::string>(), "space-separated list of the SkypeIDs to output; defaults to blank which outputs all contacts" )
	;
	
	// parse and verify command line parameters
	po::variables_map vm;
	try{
		po::store( po::parse_command_line( argc, argv, desc ), vm );
		po::notify( vm );
	}catch(...){ // malformatted input of some kind, so just show the user the help and exit the program
		std::cout << "Invalid parameters!\n\n" << desc << "\n";
		return 1;
	}

	// show the help and exit if that's what they asked for
	if( vm.count( "help" ) ){
		std::cout << desc << "\n";
		return 0;
	}

	// verify the provided database and output paths and turn them into boost filesystem objects, then create the output path if needed
	fs::path dbPath( vm["db"].as<std::string>() );
	fs::path outPath( vm["outpath"].as<std::string>() );
	dbPath.make_preferred(); // formats all slashes according to operating system
	outPath.make_preferred();
	try{
		if( !fs::exists( dbPath ) || !fs::is_regular_file( dbPath ) ){
			std::cout << "Database " << dbPath.leaf() << " does not exist at the provided path!\n\n" << desc << "\n";
			return 1;
		}
		if( fs::file_size( dbPath ) == 0 ){
			std::cout << "Database " << dbPath.leaf() << " is empty!\n\n" << desc << "\n";
			return 1;
		}
		if( fs::exists( outPath ) && !fs::is_directory( outPath ) ){
			std::cout << "Output path " << outPath << " already exists and is not a directory!\n\n" << desc << "\n";
			return 1;
		}else if( !fs::exists( outPath ) ){
			// outPath either exists and is a directory, or doesn't exist.
			// we must now create the path if missing. will throw an exception on errors such as lack of write permissions.
			fs::create_directories( outPath ); // creates any missing directories in the given path.
		}
	}catch( const fs::filesystem_error &ex ){
		std::cout << ex.what() << "\n";
		return 1;
	}

	// if they've provided a space-separated list of contacts to output, we need to tokenize it and store the SkypeIDs
	std::map<std::string,bool> outputContacts; // filled with all contacts to output, or blank to output all
	std::map<std::string,bool>::iterator outputContacts_it;
	if( vm.count( "contacts" ) ){
		boost::char_separator<char> sep( " " );
		boost::tokenizer< boost::char_separator<char> > tokens( vm["contacts"].as<std::string>(), sep );
		for( boost::tokenizer< boost::char_separator<char> >::iterator identities_it( tokens.begin() ); identities_it != tokens.end(); ++identities_it ){
			outputContacts_it = outputContacts.find( (*identities_it) );
			if( outputContacts_it == outputContacts.end() ){ // makes sure we only add each unique skypeID once, even if the idiot user has provided it multiple times
				outputContacts.insert( std::pair<std::string,bool>( (*identities_it), false ) ); // NOTE: we initialize the skypeID as false, and will set it to true if it's been output
			}
		}
	}

	// alright, let's begin output...
	try{
		// open Skype history database
		SkypeParser::CSkypeParser sp( dbPath.string() );
		
		// display all options (input database, output path, and all names to output (if specified))
		std::cout << "Skype History Exporter v0.91 Beta\n"
		          << "  DATABASE: [ " << dbPath << " ]\n"
		          << "    OUTPUT: [ " << outPath << " ]\n";
		if( outputContacts.size() > 0 ){
			std::cout << "  CONTACTS: [ \"";
			for( std::map<std::string,bool>::const_iterator it( outputContacts.begin() ); it != outputContacts.end(); ++it ){
				std::cout << (*it).first;
				if( boost::next( it ) != outputContacts.end() ){ std::cout << "\", \""; } // appended after every element except the last one
			}
			std::cout << "\" ]\n\n";
		}else{
			std::cout << "  CONTACTS: [ \"*\" ]\n\n";
		}

		// grab a list of all contacts encountered in the database
		const SkypeParser::skypeIDs_t &users = sp.getSkypeUsers();

		// output statistics
		std::cout << "Found " << users.size() << " contacts in the database...\n\n";

		// output contacts, skipping some in case the user provided a list of contacts to export
		for( SkypeParser::skypeIDs_t::const_iterator it( users.begin() ); it != users.end(); ++it ){
			const std::string &skypeID = (*it);
			
			// skip if we're told to filter contacts
			outputContacts_it = outputContacts.find( (*it) ); // store iterator here since we'll set it to true after outputting, if contact filters are enabled
			if( outputContacts.size() > 0 && ( outputContacts_it == outputContacts.end() ) ){ continue; } // if no filters, it's always false; if filters it's true if the contact is to be skipped
			
			// construct the final path to the log file for this user
			fs::path logPath( outPath );
			logPath /= ( (*it) + ".skypelog.htm" ); // appends the log filename and chooses the appropriate path separator

			// output exporting header
			std::cout << " * Exporting: " << skypeID << " (" << sp.getDisplayNameAtTime( skypeID, -1 ) << ")\n";
			std::cout << "   => " << logPath << "\n";
			sp.exportUserHistory( skypeID, logPath.string() );
			if( outputContacts.size() > 0 ){ (*outputContacts_it).second = true; } // since filters are enabled and we've come here, we know we've output the person as requested, so mark them as such
		}
	}catch( const std::exception &e ){
		std::cout << "Error while processing Skype database: \"" << e.what() << "\".\n";
		return 1;
	}

	// check for any missing IDs if filtered output was requested
	for( std::map<std::string,bool>::const_iterator it( outputContacts.begin() ); it != outputContacts.end(); ++it ){
		if( (*it).second == false ){ // a requested ID that was not found in the database
			std::cout << " * Not Found: " << (*it).first << "\n";
		}
	}

	std::cout << "\nExport finished.\n";
	
	return 0;
}