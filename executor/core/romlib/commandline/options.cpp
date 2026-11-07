#include "options.h"

#include <iomanip>
#include <ostream>

namespace cmdline
{

void options_description::add_option(const char *names, value_semantic *v, const char *help)
{
    option o;
    std::string n = names;
    size_t comma = n.find(',');
    o.name = n.substr(0, comma);
    if(comma != std::string::npos)
        o.letter = n.substr(comma + 1);
    o.help = help;
    o.value.reset(v);
    options_.push_back(std::move(o));
}

options_description::option *options_description::find(const std::string &name, bool letter)
{
    for(option &o : options_)
        if(letter ? o.letter == name : o.name == name)
            return &o;
    for(options_description *g : groups_)
        if(option *o = g->find(name, letter))
            return o;
    return nullptr;
}

void options_description::finish()
{
    for(option &o : options_)
        o.value->finish();
    for(options_description *g : groups_)
        g->finish();
}

std::vector<std::string> options_description::parse(int argc, char **argv)
{
    std::vector<std::string> rest;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        bool is_long = a.size() > 2 && a.compare(0, 2, "--") == 0;
        bool is_short = !is_long && a.size() == 2 && a[0] == '-' && a[1] != '-';
        if(!is_long && !is_short)
        {
            rest.push_back(a);
            continue;
        }
        std::string name = a.substr(is_long ? 2 : 1);
        std::optional<std::string> arg;
        if(is_long)
        {
            size_t eq = name.find('=');
            if(eq != std::string::npos)
            {
                arg = name.substr(eq + 1);
                name.erase(eq);
            }
        }
        option *o = find(name, is_short);
        if(!o)
        {
            rest.push_back(a); /* unregistered: the caller's */
            continue;
        }
        value_semantic &v = *o->value;
        if(!v.takes_argument())
        {
            if(arg)
                throw error("option '--" + o->name + "' does not take any arguments");
            v.parse("--" + o->name, nullptr);
            continue;
        }
        /* The next word is the argument, unless it may be left out: then
           only --name=value gives one. */
        if(!arg && i + 1 < argc && !v.argument_optional())
            arg = argv[++i];
        if(!arg && !v.argument_optional())
            throw error("the required argument for option '--" + o->name + "' is missing");
        v.parse("--" + o->name, arg ? &*arg : nullptr);
    }
    finish();
    return rest;
}

void options_description::print(std::ostream &out) const
{
    if(!title_.empty())
        out << title_ << ":\n";
    for(const option &o : options_)
    {
        std::string left = "  ";
        if(!o.letter.empty())
            left += "-" + o.letter + " [ --" + o.name + " ]";
        else
            left += "--" + o.name;
        if(o.value->takes_argument())
            left += o.value->argument_optional() ? " [arg]" : " arg";
        out << std::left << std::setw(30) << left;
        if(left.size() >= 30)
            out << "\n" << std::string(30, ' ');
        out << o.help << "\n";
    }
    for(const options_description *g : groups_)
    {
        out << "\n";
        g->print(out);
    }
}

}
