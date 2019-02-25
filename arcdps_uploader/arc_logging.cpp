#include "arc_logging.h"
#include <fstream>
#include <iostream>
#include <ShlObj.h>
#include <ctime>

arc_logging::arc_logging(char* arcvers)
	: cbtcount(0)
{
	/* logging */
	WCHAR my_documents[MAX_PATH];
	HRESULT result = SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, my_documents);

	if (result != S_OK) {
		std::cout << "Error: " << result << "\n";
	}
	else {
		std::cout << "Path: " << my_documents << "\n";
	}

	CHAR utf_path[MAX_PATH];
	WideCharToMultiByte(CP_UTF8, 0, my_documents, -1, utf_path, MAX_PATH, NULL, NULL);
	doc_path = std::string(utf_path);

	std::time_t t = std::time(nullptr);
	char timestr[100];
	std::string formatted_time;
	if (std::strftime(timestr, sizeof(timestr), "%I-%M-%S%p %b-%d-%Y", std::localtime(&t))) {
		formatted_time = std::string(timestr);
	}

	combat_log.open(doc_path + "\\arc_raw_log " + formatted_time + ".txt");
	if (!combat_log) {
		std::cout << "Opening file failed" << std::endl;
	}

	/* big buffer */
	char buff[4096];
	char* p = &buff[0];
	p += _snprintf(p, 400, "==== mod_init ====\n");
	p += _snprintf(p, 400, "arcdps: %s\n", arcvers);

	combat_log << buff;
}


arc_logging::~arc_logging()
{
	combat_log.close();
}

void arc_logging::on_combat(cbtevent * ev, ag * src, ag * dst, char * skillname)
{
	/* big buffer */
	char buff[4096];
	char* p = &buff[0];

	/* ev is null. dst will only be valid on tracking add. skillname will also be null */
	if (!ev) {

		/* notify tracking change */
		if (!src->elite) {

			/* add */
			if (src->prof) {
				p += _snprintf(p, 400, "==== cbtnotify ====\n");
				// self flag disabled - always 1
				p += _snprintf(p, 400, "agent added: %s (%llx), prof: %u, elite: %u, self: %u\n", src->name, src->id, dst->prof, dst->elite, dst->self);
			}

			/* remove */
			else {
				p += _snprintf(p, 400, "==== cbtnotify ====\n");
				p += _snprintf(p, 400, "agent removed: %s (%llx)\n", src->name, src->id);
			}
		}

		/* notify target change */
		else if (src->elite == 1) {
			p += _snprintf(p, 400, "==== cbtnotify ====\n");
			p += _snprintf(p, 400, "new target: %llx\n", src->id);
		}
	}

	/* combat event. skillname may be null. non-null skillname will remain static until module is unloaded. refer to evtc notes for complete detail */
	else {
		/* common */
		p += _snprintf(p, 400, "==== cbtevent %u at %llu ====\n", cbtcount, ev->time);
		p += _snprintf(p, 400, "source agent: %s (%llx:%u, %lx:%lx), master: %u\n", src->name, ev->src_agent, ev->src_instid, src->prof, src->elite, ev->src_master_instid);
		if (ev->dst_agent) p += _snprintf(p, 400, "target agent: %s (%llx:%u, %lx:%lx)\n", dst->name, ev->dst_agent, ev->dst_instid, dst->prof, dst->elite);
		else p += _snprintf(p, 400, "target agent: n/a\n");

		/* statechange */
		if (ev->is_statechange) {
			p += _snprintf(p, 400, "is_statechange: %u, ", ev->is_statechange);

			switch ((cbtstatechange)ev->is_statechange)
			{
			case CBTS_NONE:
				p += _snprintf(p, 400, "NO\n");
				break;
			case CBTS_ENTERCOMBAT: // src_agent entered combat, dst_agent is subgroup
				p += _snprintf(p, 400, "ENTER COMBAT\n");
				break;
			case CBTS_EXITCOMBAT: // src_agent left combat
				p += _snprintf(p, 400, "LEAVE COMBAT\n");
				break;
			case CBTS_CHANGEUP: // src_agent is now alive
				p += _snprintf(p, 400, "NOW ALIVE\n");
				break;
			case CBTS_CHANGEDEAD: // src_agent is now dead
				p += _snprintf(p, 400, "NOW DEAD\n");
				break;
			case CBTS_CHANGEDOWN: // src_agent is now downed
				p += _snprintf(p, 400, "NOW DOWNED\n");
				break;
			case CBTS_SPAWN: // src_agent is now in game tracking range
				p += _snprintf(p, 400, "SPAWNED (TRACKING)\n");
				break;
			case CBTS_DESPAWN: // src_agent is no longer being tracked
				p += _snprintf(p, 400, "DESPAWNED (STOP TRACKING)\n");
				break;
			case CBTS_HEALTHUPDATE: // src_agent has reached a health marker. dst_agent = percent * 10000 (eg. 99.5% will be 9950)
				p += _snprintf(p, 400, "HEALTH UPDATE\n");
				break;
			case CBTS_LOGSTART: // log start. value = server unix timestamp **uint32**. buff_dmg = local unix timestamp. src_agent = 0x637261 (arcdps id)
				p += _snprintf(p, 400, "LOG START\n");
				break;
			case CBTS_LOGEND: // log end. value = server unix timestamp **uint32**. buff_dmg = local unix timestamp. src_agent = 0x637261 (arcdps id)
				p += _snprintf(p, 400, "LOG END\n");
				break;
			case CBTS_WEAPSWAP: // src_agent swapped weapon set. dst_agent = current set id (0/1 water, 4/5 land)
				p += _snprintf(p, 400, "WEAP SWAP\n");
				break;
			case CBTS_MAXHEALTHUPDATE: // src_agent has had it's maximum health changed. dst_agent = new max health
				p += _snprintf(p, 400, "MAX HEALTH UPDATE\n");
				break;
			case CBTS_POINTOFVIEW: // src_agent will be agent of "recording" player
				p += _snprintf(p, 400, "POV\n");
				break;
			case CBTS_LANGUAGE: // src_agent will be text language
				p += _snprintf(p, 400, "LANG\n");
				break;
			case CBTS_GWBUILD: // src_agent will be game build
				p += _snprintf(p, 400, "GWBUILD - %llu\n", ev->src_agent);
				break;
			case CBTS_SHARDID: // src_agent will be sever shard id
				p += _snprintf(p, 400, "SHARD ID - %llu\n", ev->src_agent);
				break;
			case CBTS_REWARD: // src_agent is self, dst_agent is reward id, value is reward type. these are the wiggly boxes that you get
				p += _snprintf(p, 400, "REWARD - ID: %llu, Type: %u\n", ev->dst_agent, ev->value);
				break;
			default:
				p += _snprintf(p, 400, "\n");
				break;
			}
		}

		/* activation */
		else if (ev->is_activation) {
			p += _snprintf(p, 400, "is_activation: %u\n", ev->is_activation);
			p += _snprintf(p, 400, "skill: %s:%u\n", skillname, ev->skillid);
			p += _snprintf(p, 400, "ms_expected: %d\n", ev->value);
		}

		/* buff remove */
		else if (ev->is_buffremove) {
			p += _snprintf(p, 400, "is_buffremove: %u\n", ev->is_buffremove);
			p += _snprintf(p, 400, "skill: %s:%u\n", skillname, ev->skillid);
			p += _snprintf(p, 400, "ms_duration: %d\n", ev->value);
			p += _snprintf(p, 400, "ms_intensity: %d\n", ev->buff_dmg);
		}

		/* buff */
		else if (ev->buff) {

			/* damage */
			if (ev->buff_dmg) {
				p += _snprintf(p, 400, "is_buff: %u\n", ev->buff);
				p += _snprintf(p, 400, "skill: %s:%u\n", skillname, ev->skillid);
				p += _snprintf(p, 400, "dmg: %d\n", ev->buff_dmg);
				p += _snprintf(p, 400, "is_shields: %u\n", ev->is_shields);
			}

			/* application */
			else {
				p += _snprintf(p, 400, "is_buff: %u\n", ev->buff);
				p += _snprintf(p, 400, "skill: %s:%u\n", skillname, ev->skillid);
				p += _snprintf(p, 400, "raw ms: %d\n", ev->value);
				p += _snprintf(p, 400, "overstack ms: %u\n", ev->overstack_value);
			}
		}

		/* physical */
		else {
			p += _snprintf(p, 400, "is_buff: %u\n", ev->buff);
			p += _snprintf(p, 400, "skill: %s:%u\n", skillname, ev->skillid);
			p += _snprintf(p, 400, "dmg: %d\n", ev->value);
			p += _snprintf(p, 400, "is_moving: %u\n", ev->is_moving);
			p += _snprintf(p, 400, "is_ninety: %u\n", ev->is_ninety);
			p += _snprintf(p, 400, "is_flanking: %u\n", ev->is_flanking);
			p += _snprintf(p, 400, "is_shields: %u\n", ev->is_shields);
		}

		/* common */
		p += _snprintf(p, 400, "iff: %u\n", ev->iff);
		p += _snprintf(p, 400, "result: %u\n", ev->result);
		cbtcount += 1;
	}

	/* print */
	combat_log << buff;
}
