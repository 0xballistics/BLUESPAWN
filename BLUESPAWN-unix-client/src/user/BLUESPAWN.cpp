#include "user/bluespawn.h"
#include "user/CLI.h"
#include "util/log/HuntLogMessage.h"
#include "util/log/DebugSink.h"
#include "util/log/XMLSink.h"
#include "common/DynamicLinker.h"
#include "common/StringUtils.h"
#include "util/eventlogs/EventLogs.h"
#include "reaction/SuspendProcess.h"
#include "reaction/CarveMemory.h"
#include "reaction/DeleteFile.h"
#include "reaction/QuarantineFile.h"
#include "util/permissions/permissions.h"
#include "reaction/Log.h"

#include <sys/auxv.h>
#include <sys/utsname.h>

#pragma warning(push)

#pragma warning(disable : 26451)
#pragma warning(disable : 26444)

#include "cxxopts.hpp"

#pragma warning(pop)

#include <iostream>
#include <cmath>

#include "mitigation/mitigations/MitigateM1044.h"

const IOBase& Bluespawn::io = CLI::GetInstance();
HuntRegister Bluespawn::huntRecord{ io };
MitigationRegister Bluespawn::mitigationRecord{ io };

Bluespawn::Bluespawn(){
	mitigationRecord.RegisterMitigation(std::make_shared<Mitigations::MitigateM1044>());
}

void Bluespawn::dispatch_hunt(Aggressiveness aHuntLevel, vector<string> vExcludedHunts, vector<string> vIncludedHunts) {
	Bluespawn::io.InformUser("Starting a Hunt");
	unsigned int tactics = UINT_MAX;
	unsigned int dataSources = UINT_MAX;
	unsigned int affectedThings = UINT_MAX;
	Scope scope{};

	huntRecord.RunHunts(tactics, dataSources, affectedThings, scope, aHuntLevel, reaction, vExcludedHunts, vIncludedHunts);
}

void Bluespawn::dispatch_mitigations_analysis(MitigationMode mode, bool bForceEnforce) {
	if (mode == MitigationMode::Enforce) {
		Bluespawn::io.InformUser("Enforcing Mitigations");
		mitigationRecord.EnforceMitigations(SecurityLevel::High, bForceEnforce);
	}
	else {
		Bluespawn::io.InformUser("Auditing Mitigations");
		mitigationRecord.AuditMitigations(SecurityLevel::High);
	}
}

void Bluespawn::monitor_system(Aggressiveness aHuntLevel) {
	unsigned int tactics = UINT_MAX;
	unsigned int dataSources = UINT_MAX;
	unsigned int affectedThings = UINT_MAX;
	Scope scope{};

	Bluespawn::io.InformUser("Monitoring the system");
	huntRecord.SetupMonitoring(aHuntLevel, reaction);

	/*HandleWrapper hRecordEvent{ CreateEventW(nullptr, false, false, "Local\\FlushLogs") };
	while (true) {
		SetEvent(hRecordEvent);
		Sleep(5000);
	}*/
	//TODO: port above
}

void Bluespawn::SetReaction(const Reaction& reaction){
	this->reaction = reaction;
}

void print_help(cxxopts::ParseResult result, cxxopts::Options options) {
	std::string help_category = result["help"].as < std::string >();

	std::transform(help_category.begin(), help_category.end(),
		help_category.begin(), [](unsigned char c) { return std::tolower(c); });

	if(help_category.compare("hunt") == 0) {
		std::cout << (options.help({ "hunt" })) << std::endl;
	} else if(help_category.compare("general") == 0) {
		std::cout << (options.help()) << std::endl;
	} else {
		std::cerr << ("Unknown help category") << std::endl;
	}
}

#define X64_IDENT "x86_64"
#define X32_IDENT "i686"
/**
 * NOTE: might need above for better accuracy
 * since x64 cant run on 32 bit if the hardware identifiers arent equal unless its not x32 or x86_64 system (check later)
 * then its a 32 bit running on 64 bit hardware
 * 
 * While currently there is no reason this will not work, it is highly likely 32 bit in general will not be supported due to process and kernel level checks
 * at least until i am able to figure out x32 kernel level things
 */
void Bluespawn::check_correct_arch() {
    unsigned long atr = getauxval(AT_PLATFORM);
	struct utsname name;
	int ures = uname(&name);
	if(!atr || ures != 0){
		LOG_ERROR("Unable to get hardware specifications");
	}else{
		if(std::string((char*)atr) != std::string(name.machine)){
			Bluespawn::io.AlertUser("Running the x86 version of BLUESPAWN on an x64 system! This configuration is not fully supported, so we recommend downloading the x64 version.", 5000, ImportanceLevel::MEDIUM);
			LOG_WARNING("Running the x86 version of BLUESPAWN on an x64 system! This configuration is not fully supported, so we recommend downloading the x64 version.");
		}
	}
}

int main(int argc, char* argv[]){
	Bluespawn bluespawn{};

	print_banner();

	bluespawn.check_correct_arch();

	cxxopts::Options options("BLUESPAWN", "BLUESPAWN: A Windows based Active Defense Tool to empower Blue Teams");

	options.add_options()
		("h,hunt", "Perform a Hunt Operation", cxxopts::value<bool>())
		("n,monitor", "Monitor the System for Malicious Activity. Available options are Cursory, Normal, or Intensive.", cxxopts::value<std::string>()->implicit_value("Normal"))
		("m,mitigate", "Mitigates vulnerabilities by applying security settings. Available options are audit and enforce.", cxxopts::value<std::string>()->implicit_value("audit"))
		("help", "Help Information. You can also specify a category for help on a specific module such as hunt.", cxxopts::value<std::string>()->implicit_value("general"))
		("log", "Specify how Bluespawn should log events. Options are console (default), xml, and debug.", cxxopts::value<std::string>()->default_value("console"))
		("reaction", "Specifies how bluespawn should react to potential threats dicovered during hunts.", cxxopts::value<std::string>()->default_value("log"))
		("v,verbose", "Verbosity", cxxopts::value<int>()->default_value("0"))
		("debug", "Enable Debug Output", cxxopts::value<bool>())
		;

	options.add_options("hunt")
		("l,level", "Aggressiveness of Hunt. Either Cursory, Normal, or Intensive", cxxopts::value<std::string>())
		("hunts", "List of hunts to run by Mitre ATT&CK name. Will only run these hunts.", cxxopts::value<std::vector<std::string>>())
		("exclude-hunts", "List of hunts to avoid running by Mitre ATT&CK name. Will run all hunts but these.", cxxopts::value<std::vector<std::string>>())
		;

	options.add_options("mitigate")
		("force", "Use this option to forcibly apply mitigations with no prompt", cxxopts::value<bool>())
		;

	options.parse_positional({ "level" });
	try {
		auto result = options.parse(argc, argv);

		if (result.count("verbose")) {
			if(result["verbose"].as<int>() >= 1) {
				Log::LogLevel::LogVerbose1.Enable();
			}
			if(result["verbose"].as<int>() >= 2) {
				Log::LogLevel::LogVerbose2.Enable();
			}
			if(result["verbose"].as<int>() >= 3) {
				Log::LogLevel::LogVerbose3.Enable();
			}
		}

		auto sinks = result["log"].as<std::string>();
		std::set<std::string> sink_set;
		for(unsigned startIdx = 0; startIdx < sinks.size();){
			auto endIdx = min(sinks.find(',', startIdx), sinks.size());
			auto sink = sinks.substr(startIdx, endIdx - startIdx);
			sink_set.emplace(sink);
			startIdx = endIdx + 1;
		}
		for(auto sink : sink_set){
			if(sink == "console"){
				auto Console = std::make_shared<Log::CLISink>();
				Log::AddHuntSink(Console);
				if(result.count("debug")) Log::AddSink(Console);
			} else if(sink == "xml"){
				auto XMLSink = std::make_shared<Log::XMLSink>();
				Log::AddHuntSink(XMLSink);
				if(result.count("debug")) Log::AddSink(XMLSink);
			} else if(sink == "debug"){
				auto DbgSink = std::make_shared<Log::DebugSink>();
				Log::AddHuntSink(DbgSink);
				if(result.count("debug")) Log::AddSink(DbgSink);
			} else {
				bluespawn.io.AlertUser("Unknown log sink \"" + sink + "\"", -1, ImportanceLevel::MEDIUM);
			}
		}

		if (result.count("help")) {
			print_help(result, options);
		}

		else if (result.count("hunt") || result.count("monitor")) {
			std::map<std::string, Reaction> reactions = {
				{"log", Reactions::LogReaction{}},
				{"suspend", Reactions::SuspendProcessReaction{ bluespawn.io }},
				{"carve-memory", Reactions::CarveProcessReaction{ bluespawn.io }},
				{"delete-file", Reactions::DeleteFileReaction{ bluespawn.io }},
				{"quarantine-file", Reactions::QuarantineFileReaction{ bluespawn.io}},
			};

			auto UserReactions = result["reaction"].as<std::string>();
			std::set<std::string> reaction_set;
			for(unsigned startIdx = 0; startIdx < UserReactions.size();){
				auto endIdx = min(UserReactions.find(',', startIdx), UserReactions.size());
				auto sink = UserReactions.substr(startIdx, endIdx - startIdx);
				reaction_set.emplace(sink);
				startIdx = endIdx + 1;
			}

			Reaction combined = {};
			for(auto reaction : reaction_set){
				if(reactions.find(reaction) != reactions.end()){
					combined.Combine(reactions[reaction]);
				} else {
					bluespawn.io.AlertUser("Unknown reaction \"" + reaction + "\"", -1, ImportanceLevel::MEDIUM);
				}
			}

			bluespawn.SetReaction(combined);

			// Parse the hunt level
			std::string sHuntLevelFlag = "Normal";
			Aggressiveness aHuntLevel;
			try {
				sHuntLevelFlag = result["level"].as < std::string >();
			}
			catch (int e) {}

			if (CompareIgnoreCase<std::string>(sHuntLevelFlag, "Cursory")) {
				aHuntLevel = Aggressiveness::Cursory;
			}
			else if (CompareIgnoreCase<std::string>(sHuntLevelFlag, "Normal")) {
				aHuntLevel = Aggressiveness::Normal;
			}
			else if (CompareIgnoreCase<std::string>(sHuntLevelFlag, "Intensive")) {
				aHuntLevel = Aggressiveness::Intensive;
			}
			else {
				LOG_ERROR("Error " << sHuntLevelFlag << " - Unknown level. Please specify either Cursory, Normal, or Intensive");
				LOG_ERROR("Will default to Cursory for this run.");
				Bluespawn::io.InformUser("Error " + sHuntLevelFlag + " - Unknown level. Please specify either Cursory, Normal, or Intensive");
				Bluespawn::io.InformUser("Will default to Cursory.");
				aHuntLevel = Aggressiveness::Cursory;
			}

			//Parse included and excluded hunts
			std::vector<std::string> vIncludedHunts;
			std::vector<std::string> vExcludedHunts;

			if (result.count("hunts")) {
				vIncludedHunts = result["hunts"].as<std::vector<std::string>>();
			}
			else if (result.count("exclude-hunts")) {
				vExcludedHunts = result["exclude-hunts"].as<std::vector<std::string>>();
			}

			if (result.count("hunt"))
				bluespawn.dispatch_hunt(aHuntLevel, vExcludedHunts, vIncludedHunts);
			else if (result.count("monitor"))
				bluespawn.monitor_system(aHuntLevel);

		}
		else if (result.count("mitigate")) {
			bool bForceEnforce = false;
			if (result.count("force"))
				bForceEnforce = true;

			MitigationMode mode = MitigationMode::Audit;
			if (result["mitigate"].as<std::string>() == "e" || result["mitigate"].as<std::string>() == "enforce")
				mode = MitigationMode::Enforce;

			bluespawn.dispatch_mitigations_analysis(mode, bForceEnforce);
		}
		else {
			LOG_ERROR("Nothing to do. Use the -h or --hunt flags to launch a hunt");
		}
	}
	catch (cxxopts::OptionParseException e1) {
		LOG_ERROR(e1.what());
	}

	return 0;
}
