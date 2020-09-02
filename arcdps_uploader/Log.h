#pragma once

#include <string>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <Revtc.h>
#include "sqlite_orm.h"


struct Log {
	int id;
	std::filesystem::path path;
	std::string filename;
	std::string human_time;
	std::chrono::system_clock::time_point time;
	bool uploaded;
	bool error;
	std::string report_id;
	std::string permalink;
	int boss_id;
	std::string boss_name;
	std::string players_json;
	bool json_available;
	bool success;

	inline bool operator==(const Log&rhs) {
		return time == rhs.time && filename == rhs.filename;
	}

	friend inline bool operator>(const Log& lhs, const Log& rhs) {
		return lhs.time > rhs.time;
	}
};

std::string PathToString(std::filesystem::path path);
std::unique_ptr<std::filesystem::path> PathFromString(const std::string& s);
std::string TimepointToString(std::chrono::system_clock::time_point tp);
std::unique_ptr<std::chrono::system_clock::time_point> TimepointFromString(const std::string& s);

namespace sqlite_orm
{
	//std::filesystem::path
	template<>
	struct type_printer<std::filesystem::path> : public text_printer {};
	
	template<>
	struct statement_binder<std::filesystem::path>
	{
		int bind(sqlite3_stmt* stmt, int index, const std::filesystem::path& path)
		{
			return statement_binder<std::string>().bind(stmt, index, PathToString(path));
		}
	};

	template<>
	struct field_printer<std::filesystem::path> {
		std::string operator()(const std::filesystem::path& t) const
		{
			return PathToString(t);
		}
	};

	template<>
	struct row_extractor<std::filesystem::path> {
		std::filesystem::path extract(const char* row_value) {
			if (auto path = PathFromString(row_value)) {
				return *path;
			}
			else
			{
				throw std::runtime_error("incorrect path string (" + std::string(row_value) + ")");
			}
		}

		std::filesystem::path extract(sqlite3_stmt* stmt, int columnIndex)
		{
			auto str = sqlite3_column_text(stmt, columnIndex);
			return this->extract((const char*)str);
		}
	};

	//std::chrono::system_clock::time_point
	using namespace std::chrono;
	template<>
	struct type_printer<system_clock::time_point> : public text_printer {};

	template<>
	struct statement_binder<system_clock::time_point>
	{
		int bind(sqlite3_stmt* stmt, int index, const system_clock::time_point& value)
		{
			return statement_binder<std::string>().bind(stmt, index, TimepointToString(value));
		}
	};

	template<>
	struct field_printer<system_clock::time_point>
	{
		std::string operator()(const system_clock::time_point& t) const
		{
			return TimepointToString(t);
		}
	};

	template<>
	struct row_extractor<system_clock::time_point> {
		system_clock::time_point extract(const char* row_value) {
			if (auto path = TimepointFromString(row_value)) {
				return *path;
			}
			else
			{
				throw std::runtime_error("incorrect path string (" + std::string(row_value) + ")");
			}
		}

		system_clock::time_point extract(sqlite3_stmt* stmt, int columnIndex)
		{
			auto str = sqlite3_column_text(stmt, columnIndex);
			return this->extract((const char*)str);
		}
	};
}
