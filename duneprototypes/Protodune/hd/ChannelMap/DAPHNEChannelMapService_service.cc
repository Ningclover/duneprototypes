#include "DAPHNEChannelMapService.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

dune::DAPHNEChannelMapService::DAPHNEChannelMapService(fhicl::ParameterSet const& pset) {

  std::string channelMapFile = pset.get<std::string>("FileName");

  std::string fullname;
  cet::search_path sp("FW_SEARCH_PATH");
  sp.find_file(channelMapFile, fullname);

  if (fullname.empty()) {
    std::cout << "Input file " << channelMapFile << " not found" << std::endl;
    throw cet::exception("File not found");
  }
  else
    std::cout << "DAPHNE Channel Map: Building TPC wiremap from file " << channelMapFile << std::endl;

  fChannelMap.ReadMapFromFile(fullname);
}

dune::DAPHNEChannelMapService::DAPHNEChannelMapService(fhicl::ParameterSet const& pset, art::ActivityRegistry&) : DAPHNEChannelMapService(pset) {}

unsigned int dune::DAPHNEChannelMapService::GetOfflineChannel(
    unsigned int slot,
    unsigned int link,
    unsigned int daphne_channel) const {

  return fChannelMap.GetOfflineChannel(slot, link, daphne_channel);
}

DEFINE_ART_SERVICE(dune::DAPHNEChannelMapService)
