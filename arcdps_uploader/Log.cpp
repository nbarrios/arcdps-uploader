#include "Log.h"

std::string PathToString(std::filesystem::path path)
{
	return path.string();
}

std::unique_ptr<std::filesystem::path> PathFromString(const std::string& s)
{
	return std::make_unique<std::filesystem::path>(std::filesystem::path(s));
}

std::string TimepointToString(std::chrono::system_clock::time_point tp)
{
	auto t = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
	return std::to_string(t);
}

std::unique_ptr<std::chrono::system_clock::time_point> TimepointFromString(const std::string& s)
{
	long long t = std::stoll(s);
	return std::make_unique<std::chrono::system_clock::time_point>(
		std::chrono::system_clock::time_point(std::chrono::seconds(t))
	);
}
