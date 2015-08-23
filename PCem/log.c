#include <cstdarg>

#include "log.h"

static uint8_t log_filter;
static std::vector<std::string> comp_filter;

//All you need for logging is slight upgrade of printf.
void define_log_filter(uint8_t filter)
{
#ifndef RELEASE_BUILD
	log_filter = filter;
#endif
}

void log_print(char *component, uint8_t level, char *msg, ...)
{
#ifndef RELEASE_BUILD
    std::string stds_component(component);
    std::string stds_msg(msg);

    if(!(level & log_filter)) return;

    va_list args;
    va_start(args, msg.c_str());

    std::string level_str;
    switch(level)
    {
    case LOG_LEVEL_ERROR:
    {
        level_str = "Error";
        break;
    }
    case LOG_LEVEL_WARNING:
    {
        level_str = "Warning";
        break;
    }
    case LOG_LEVEL_DEBUG:
    {
        level_str = "Debug";
        break;
    }
    case LOG_LEVEL_VERBOSE:
    {
        level_str = "Verbose";
        break;
    }
    case LOG_LEVEL_INFO:
    {
        level_str = "Info";
        break;
    }
    }

	for(int i = 0;i<comp_filter.size();i++)
	{
		if(stds_component == comp_filter[i]) return;
	}

    std::string final_msg = "[" + stds_component + " | " + level_str + "] " + stds_msg + "\n";

    vprintf(final_msg.c_str(), args);

    va_end(args);
#endif
}

