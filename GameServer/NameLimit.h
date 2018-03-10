#pragma once

#include <memory>
#include <functional>
#include <fstream>
#include <vector>
#include <string>

#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <boost/algorithm/searching/boyer_moore.hpp>
#include <boost/algorithm/string.hpp>

#include "P_Header.h"
#include "CommonUtil.h"
#include "MXLog.h"

namespace Adoter
{
namespace pb = google::protobuf;
using namespace boost::algorithm;

class NameLimit : public std::enable_shared_from_this<NameLimit>   
{
	Asset::PingBi _pingbi;
	std::vector<std::string> _names;
public:
	static NameLimit& Instance()
	{
		static NameLimit _instance;
		return _instance;
	}

	bool Load()
	{
		std::ifstream fi("PingBi.txt", std::ifstream::in);
		pb::io::IstreamInputStream iis(&fi);

		auto result = pb::TextFormat::Parse(&iis, &_pingbi);
		fi.close();

		if (!result) return false;

		for (const auto& name : _pingbi.name()) _names.push_back(name);
		
		return true;
	}

	bool IsValid(std::string name)
	{
		boost::trim(name); 

		//boyer_moore<std::string::const_iterator, std::string::const_iterator> search(name.begin(), name.end());

		//search(_names.begin(), _names.end());
		/*
		if (it != _names.end()) 
		{
			ERROR("名字:{} 非法", name);
			return false;
		}
		*/

		return true; 
	}
};

#define NameInstance NameLimit::Instance()

}
