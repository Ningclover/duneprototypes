////////////////////////////////////////////////////////////////////////
// Class:       HDColdboxDAQWriter
// Plugin Type: analyzer (Unknown Unknown)
// File:        HDColdboxDAQWriter_module.cc
//
//   Module to emulate DAQ-formatted writing of raw::RawDigits in 
//     HDF5 format
// Generated at Fri Aug 19 16:42:07 2022 by Thomas Junk using cetskelgen
// from  version .
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include <hdf5.h>
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <ios>
#include <iomanip>
#include <vector>
#include <map>
//  #include "daqdataformats/TriggerRecordHeader.hpp"
#include "daqdataformats/Fragment.hpp"
#include "detdataformats/wib2/WIB2Frame.hpp"
#include "lardataobj/RawData/raw.h"
#include "lardataobj/RawData/RawDigit.h"
#include "duneprototypes/Coldbox/hd/ChannelMap/PD2HDChannelMapService.h"
#include "dunecore/HDF5Utils/HDF5Utils.h"

class HDColdboxDAQWriter;


class HDColdboxDAQWriter : public art::EDAnalyzer {
public:
  explicit HDColdboxDAQWriter(fhicl::ParameterSet const& p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  HDColdboxDAQWriter(HDColdboxDAQWriter const&) = delete;
  HDColdboxDAQWriter(HDColdboxDAQWriter&&) = delete;
  HDColdboxDAQWriter& operator=(HDColdboxDAQWriter const&) = delete;
  HDColdboxDAQWriter& operator=(HDColdboxDAQWriter&&) = delete;

  void analyze(art::Event const& e) override;
  void beginRun(art::Run const& run) override;
  void endRun(art::Run const& run) override;

private:

  void addStringAttribute(hid_t fp, std::string attrname, std::string attrval);
  void addU64Attribute(hid_t fp,  std::string attrname, uint64_t value);
  void addU32Attribute(hid_t fp,  std::string attrname, uint32_t value);

  hid_t fFilePtr;
  std::string fOutfilename;
  std::string fRawDigitLabel;
  std::string fOperationalEnvironment;
  size_t fBytesWritten;

};


HDColdboxDAQWriter::HDColdboxDAQWriter(fhicl::ParameterSet const& p)
  : EDAnalyzer{p}  // ,
// More initializers here.
{
  fOutfilename = p.get<std::string>("filename","hdcoldboxrawsim.hdf5");
  fRawDigitLabel = p.get<std::string>("rawdigitlabel","tpcrawdecoder:daq");
  fOperationalEnvironment = p.get<std::string>("operational_environment","np04_coldbox");
  fFilePtr = H5I_INVALID_HID;
}

void HDColdboxDAQWriter::analyze(art::Event const& e)
{
  art::ServiceHandle<dune::PD2HDChannelMapService> channelMap;

  auto runno = e.run();
  //auto subrun = e.subRun();
  auto evtno = e.event();

  bool warnedNegative = false;  // warn just once per event

  hid_t gpl = H5Pcreate(H5P_LINK_CREATE);
  H5Pset_char_encoding(gpl,H5T_CSET_UTF8);
  std::string trgname = "/TriggerRecord";
  std::ostringstream ofm1;
  ofm1 << std::internal << std::setfill('0') << std::setw(5) << evtno;
  trgname += ofm1.str();
  trgname += ".0000";
  hid_t trg = H5Gcreate(fFilePtr,trgname.c_str(),gpl,H5P_DEFAULT,H5P_DEFAULT);
  std::string tpcgname = trgname + "/TPC";
  hid_t tpcg = H5Gcreate(fFilePtr,tpcgname.c_str(),gpl,H5P_DEFAULT,H5P_DEFAULT);


  // this will throw an exception if the raw digits cannot be found.

  auto rawdigithandle = e.getValidHandle< std::vector<raw::RawDigit> >(fRawDigitLabel);
  std::vector< art::Ptr<raw::RawDigit> > RawDigits;
  art::fill_ptr_vector(RawDigits, rawdigithandle);

  // need a map of indices to raw digits sorted by channel number
  // check that all raw digits have the same number of samples

  std::map<uint32_t,uint32_t> rdmap;
  size_t nSamples = 0;
  for (uint32_t iptn=0; iptn<RawDigits.size(); ++iptn)
    {
      rdmap[RawDigits[iptn]->Channel()] = iptn;
      size_t nSc = RawDigits[iptn]->Samples();
      if (nSamples == 0)
	{
	  nSamples = nSc;
	}
      if (nSamples != 0 && nSamples != nSc)
	{
	  throw cet::exception("HDColdboxDAQWriter") << "raw digits have different numbers of samples: " 
						     << nSamples << " " <<  nSc << std::endl;
	}
    }
  std::vector<short> uncompressed(nSamples);

  // need a map of offline channels by link and wibframechan for each APA
  // use the first 2560 channels in the map as a template.

  const uint32_t nLinks = 10;
  uint32_t cml[nLinks][256];  // first index is link in HDF5 file, second is wibframechan
  // link goes from 0 to 9, like the dataset names in the HDF5 file
  // two links per WIB, two FEMBs per link

  for (unsigned int ichan = 0; ichan < 2560; ++ichan)
    {
      auto cinfo = channelMap->GetChanInfoFromOfflChan(ichan);
      unsigned int link = (cinfo.wib-1)*2 + cinfo.link;
      cml[link][cinfo.wibframechan] = ichan;
    }

  int curapa = -1;
  hid_t agrp = H5I_INVALID_HID;

  for (auto const &dmp : rdmap)
    {
      uint32_t channo = dmp.first;

      if (curapa == -1 || (int) channo > (curapa+1)*2560 - 1)
	{
	  curapa = channo / 2560;

          std::string agname = tpcgname + "/APA";
          std::ostringstream ofm2;
          ofm2 << std::internal << std::setfill('0') << std::setw(3) << curapa;
          agname += ofm2.str();
	  if (agrp != H5I_INVALID_HID)
	    {
	      H5Gclose(agrp);
	    }
          agrp = H5Gcreate(fFilePtr,agname.c_str(),gpl,H5P_DEFAULT,H5P_DEFAULT);
 
          for (size_t ilink=0; ilink<nLinks; ++ilink)
            {
              std::string lgname = agname + "/Link";
              std::ostringstream ofm3;
              ofm3 << std::internal << std::setfill('0') << std::setw(2) << ilink;
              lgname += ofm3.str();

	      uint32_t first_chan_on_link = cml[ilink][0] + curapa*2560;
	      auto cinfo = channelMap->GetChanInfoFromOfflChan(first_chan_on_link);
	      uint32_t crate = cinfo.crate;
	      uint32_t wib = cinfo.wib;
	      uint32_t link = cinfo.link;

              std::vector<dunedaq::detdataformats::wib2::WIB2Frame> frames(nSamples);
	      for (size_t isample=0; isample<nSamples; ++isample)
		{
		  frames.at(isample).header.version = 2;
		  frames.at(isample).header.timestamp_1 = 0;  
		  frames.at(isample).header.timestamp_2 = 25*isample;
		  frames.at(isample).header.crate = crate;
		  frames.at(isample).header.slot =  wib + 7;  // 8-1:  extra bit set to mimic WIB firmware
		  frames.at(isample).header.link =  link;
		}

	      for (size_t wibframechan = 0; wibframechan < 256; ++wibframechan)
		{
		  uint32_t offlchan = 2560*curapa + cml[ilink][wibframechan];
		  auto rdmi = rdmap.find(offlchan);
		  if (rdmi == rdmap.end())  // channel not list of raw::RawDigits.  Fill ADC values with zeros
		    {
		      for (size_t isample=0; isample<nSamples; ++isample)
			{
			  frames.at(isample).set_adc(wibframechan,0);
			}
		    }
		  else
		    {
		      int pedestal = (int) (RawDigits[rdmi->second]->GetPedestal() + 0.5);  // nearest integer
                      raw::Uncompress(RawDigits[rdmi->second]->ADCs(), uncompressed, pedestal, RawDigits[rdmi->second]->Compression());
		      for (size_t isample=0; isample<nSamples; ++isample)
			{
			  auto adc = uncompressed.at(isample);
			  if (adc < 0)
			    {
			      adc = 0;
			      if (!warnedNegative)
				{
				  MF_LOG_WARNING("FDHDDAQWriter_module") << "Negative ADC value in raw::RawDigit.  Setting to zero to put in WIB frame\n";
				  warnedNegative = true;
				}
			    }
			  frames.at(isample).set_adc(wibframechan,adc);
			}		      
		    }
		}

              dunedaq::daqdataformats::Fragment frag(&frames[0],frames.size()*sizeof(dunedaq::detdataformats::wib2::WIB2Frame));
	      frag.set_run_number(runno);
	      frag.set_trigger_number(evtno);
	      frag.set_trigger_timestamp(0);

              hid_t linkspl = H5Pcreate(H5P_LINK_CREATE);
              H5Pset_char_encoding(linkspl,H5T_CSET_UTF8);
              hsize_t linkdims[2];
              linkdims[0] = frag.get_size();
              linkdims[1] = 1;
	      fBytesWritten += frag.get_size();
              //std::cout << "frag size: " << linkdims[0] << std::endl;
              hid_t linkspace = H5Screate_simple(2,linkdims,NULL);
              hid_t linkdset = H5Dcreate2(agrp,lgname.c_str(),H5T_STD_I8LE,linkspace,linkspl,H5P_DEFAULT,H5P_DEFAULT);
              H5Dwrite(linkdset,H5T_STD_I8LE,H5S_ALL,H5S_ALL,H5P_DEFAULT,frag.get_storage_location());
              H5Dclose(linkdset);
              H5Pclose(linkspl);
              H5Sclose(linkspace);
            }
	}
    }
  // close the last APA group
  if (agrp != H5I_INVALID_HID)
    {
      H5Gclose(agrp);
    }
  H5Gclose(tpcg);

  // make our own trigger record header

  //std::vector<dunedaq::daqdataformats::ComponentRequest> crvec(2);
  //dunedaq::daqdataformats::TriggerRecordHeader trHeader(crvec);

  dune::HDF5Utils::HeaderInfo trhinfo;
  trhinfo.runNum = runno;
  trhinfo.trigNum = evtno;

  hid_t dspl = H5Pcreate(H5P_LINK_CREATE);
  H5Pset_char_encoding(dspl,H5T_CSET_UTF8);
  hsize_t dims[2];
  //dims[0] = trHeader.get_total_size_bytes();
  dims[0] = sizeof(trhinfo);
  dims[1] = 1;
  //std::cout << "trheader size: " << dims[0] << std::endl;
  hid_t trhspace = H5Screate_simple(2,dims,NULL);
  hid_t trdset = H5Dcreate2(trg,"TriggerRecordHeader",H5T_STD_I8LE,trhspace,dspl,H5P_DEFAULT,H5P_DEFAULT);
  H5Dwrite(trdset,H5T_STD_I8LE,H5S_ALL,H5S_ALL,H5P_DEFAULT,&trhinfo);
  H5Dclose(trdset);
  H5Pclose(dspl);
  H5Sclose(trhspace);
  H5Gclose(trg);
}

void HDColdboxDAQWriter::beginRun(art::Run const& run)
{
  auto runno = run.run();
  //auto subrun = run.subRun();

  // to think about -- do we want to append the run number to the output file name in case we
  // have more than one than one run number?  DAQ-formatted files cannot support more than one
  // run number.

  hid_t fapl = H5Pcreate(H5P_FILE_ACCESS);
  H5Pset_fclose_degree(fapl,H5F_CLOSE_STRONG);
  fFilePtr = H5Fcreate(fOutfilename.c_str(),H5F_ACC_TRUNC,H5P_DEFAULT,fapl);
  H5Pclose(fapl);
  if (fFilePtr == H5I_INVALID_HID)
    {
      throw cet::exception("HDColdboxDAQWriter") << "failed to open output file: " << fOutfilename << std::endl;
    }
  addStringAttribute(fFilePtr,"application_name","dataflow0");
  // think about timestamps currently dummy values.
  addStringAttribute(fFilePtr,"closing_timestamp","1656242791447");  // from a coldbox data file
  addStringAttribute(fFilePtr,"creation_timestamp","1656241681460");
  addU64Attribute(fFilePtr,"file_index",0);

  // from a coldbox data file

  addStringAttribute(fFilePtr,"filelayout_params","{\"digits_for_record_number\":5,\"digits_for_sequence_number\":4,\"path_param_list\":[{\"detector_group_name\":\"TPC\",\"detector_group_type\":\"TPC\",\"digits_for_element_number\":2,\"digits_for_region_number\":3,\"element_name_prefix\":\"Link\",\"region_name_prefix\":\"APA\"},{\"detector_group_name\":\"PDS\",\"detector_group_type\":\"PDS\",\"digits_for_element_number\":2,\"digits_for_region_number\":3,\"element_name_prefix\":\"Element\",\"region_name_prefix\":\"Region\"},{\"detector_group_name\":\"NDLArTPC\",\"detector_group_type\":\"NDLArTPC\",\"digits_for_element_number\":2,\"digits_for_region_number\":3,\"element_name_prefix\":\"Element\",\"region_name_prefix\":\"Region\"},{\"detector_group_name\":\"Trigger\",\"detector_group_type\":\"DataSelection\",\"digits_for_element_number\":2,\"digits_for_region_number\":3,\"element_name_prefix\":\"Element\",\"region_name_prefix\":\"Region\"}],\"record_header_dataset_name\":\"TriggerRecordHeader\",\"record_name_prefix\":\"TriggerRecord\"}");

  addU32Attribute(fFilePtr,"filelayout_version",2);
  addStringAttribute(fFilePtr,"operational_environment","np04_coldbox");
  addStringAttribute(fFilePtr,"record_type","TriggerRecord");
  addU32Attribute(fFilePtr,"run_number",runno);

  fBytesWritten = 0;  // does this include the attributes and group names and such?  For now,
                      // just add up the data sizes.

}

void HDColdboxDAQWriter::endRun(art::Run const& run)
{
  addU64Attribute(fFilePtr,"recorded_size",fBytesWritten);
  H5Fclose(fFilePtr);
  fFilePtr = H5I_INVALID_HID;
}

void HDColdboxDAQWriter::addStringAttribute(hid_t fp, std::string attrname, std::string attrval)
{
  const char *aval[1];
  aval[0] = attrval.c_str();
  hid_t aspace = H5Screate(H5S_SCALAR);
  hid_t attr_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(attr_type, H5T_VARIABLE);
  H5Tset_strpad(attr_type, H5T_STR_NULLTERM);
  H5Tset_cset(attr_type, H5T_CSET_UTF8);
  hid_t attr = H5Acreate(fp, attrname.c_str(), attr_type, aspace, H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(attr,attr_type,aval);
  H5Aclose(attr);
  H5Sclose(aspace);
  H5Tclose(attr_type);
}

void HDColdboxDAQWriter::addU64Attribute(hid_t fp,  std::string attrname, uint64_t value)
{
  hid_t aspace = H5Screate(H5S_SCALAR);
  hid_t attr_type = H5Tcopy(H5T_STD_U64LE);
  hid_t attr = H5Acreate(fp, attrname.c_str(), attr_type, aspace, H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(attr,attr_type,&value);
  H5Aclose(attr);
  H5Sclose(aspace);
  H5Tclose(attr_type);
}

void HDColdboxDAQWriter::addU32Attribute(hid_t fp,  std::string attrname, uint32_t value)
{
  hid_t aspace = H5Screate(H5S_SCALAR);
  hid_t attr_type = H5Tcopy(H5T_STD_U32LE);
  hid_t attr = H5Acreate(fp, attrname.c_str(), attr_type, aspace, H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(attr,attr_type,&value);
  H5Aclose(attr);
  H5Sclose(aspace);
  H5Tclose(attr_type);
}


DEFINE_ART_MODULE(HDColdboxDAQWriter)
