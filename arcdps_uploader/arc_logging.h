#pragma once

#include "arcdps_defs.h"
#include <string>
#include <fstream>

class arc_logging
{
	std::string doc_path;
	std::ofstream combat_log;
	uint32_t cbtcount;
public:
	arc_logging(char* arcvers);
	~arc_logging();

	void on_combat(cbtevent* ev, ag* src, ag* dst, char* skillname);
};

