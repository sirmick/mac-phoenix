#include "program_options_extended.h"

void boost::filesystem::validate(boost::any& v, const std::vector<std::string>& s, boost::filesystem::path*, int)
{
    v = boost::filesystem::path(boost::program_options::validators::get_single_string(s));
}
