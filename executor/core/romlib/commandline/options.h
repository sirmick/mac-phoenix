#pragma once

/* MacPhoenix: Executor's command line without Boost.program_options. The
 * same shape as the subset main.cpp used: option groups, bool switches,
 * typed values stored through a pointer or handed to a notifier, with a
 * validator, an implicit value or a default. Options are "--name value",
 * "--name=value" or "-x value"; anything not registered, and every
 * positional argument, is returned in order for the caller. */

#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace cmdline
{

struct error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class value_semantic
{
public:
    virtual ~value_semantic() = default;
    /* False for a switch: the option takes no argument. */
    virtual bool takes_argument() const = 0;
    /* An argument may be left out (implicit_value). */
    virtual bool argument_optional() const = 0;
    /* Seen on the command line, with its argument (empty: none given). */
    virtual void parse(const std::string &option, const std::string *arg) = 0;
    /* After parsing: store and notify, or apply the default. */
    virtual void finish() = 0;
};

namespace detail
{
/* The whole word for strings and paths; operator>> for the rest. */
template<typename T>
bool convert(const std::string &s, T &out)
{
    if constexpr(std::is_constructible_v<T, std::string>)
    {
        out = T(s);
        return true;
    }
    else
    {
        std::istringstream in(s);
        in >> out;
        return !in.fail();
    }
}
template<typename T>
bool convert(const std::string &s, std::optional<T> &out)
{
    T v;
    if(!convert(s, v))
        return false;
    out = std::move(v);
    return true;
}
}

template<typename T>
class typed_value : public value_semantic
{
public:
    typed_value() = default;
    /* Stored into *dst, converted to U. */
    template<typename U>
    explicit typed_value(U *dst)
    {
        if(dst)
            store_ = [dst](const T &v) { *dst = static_cast<U>(const_cast<T &>(v)); };
    }

    typed_value *notifier(std::function<void(const T &)> f)
    {
        notifier_ = std::move(f);
        return this;
    }
    typed_value *validator(std::function<bool(const T &)> f)
    {
        validator_ = std::move(f);
        return this;
    }
    typed_value *implicit_value(T v)
    {
        implicit_ = std::move(v);
        return this;
    }
    typed_value *default_value(T v)
    {
        default_ = std::move(v);
        return this;
    }
    /* A switch: no argument, the implicit value when present. */
    typed_value *zero_tokens()
    {
        switch_ = true;
        return this;
    }

    bool takes_argument() const override { return !switch_; }
    bool argument_optional() const override { return switch_ || implicit_.has_value(); }

    void parse(const std::string &option, const std::string *arg) override
    {
        if(seen_)
            throw error("option '" + option + "' cannot be specified more than once");
        seen_ = true;
        if(!arg)
        {
            value_ = implicit_;
            return;
        }
        T v{};
        if(!detail::convert(*arg, v) || (validator_ && !validator_(v)))
            throw error("the argument ('" + *arg + "') for option '" + option + "' is invalid");
        value_ = std::move(v);
    }

    void finish() override
    {
        std::optional<T> v = seen_ ? value_ : default_;
        if(!v)
            return;
        if(store_)
            store_(*v);
        if(notifier_)
            notifier_(*v);
    }

private:
    std::function<void(const T &)> store_, notifier_;
    std::function<bool(const T &)> validator_;
    std::optional<T> implicit_, default_, value_;
    bool switch_ = false, seen_ = false;
};

/* A repeatable option: each occurrence is appended. */
template<typename T>
class typed_value<std::vector<T>> : public value_semantic
{
public:
    explicit typed_value(std::vector<T> *dst = nullptr) : dst_(dst) {}
    bool takes_argument() const override { return true; }
    bool argument_optional() const override { return false; }
    void parse(const std::string &option, const std::string *arg) override
    {
        T v{};
        if(!arg || !detail::convert(*arg, v))
            throw error("the argument for option '" + option + "' is invalid");
        values_.push_back(std::move(v));
    }
    void finish() override
    {
        if(dst_)
            dst_->insert(dst_->end(), values_.begin(), values_.end());
    }

private:
    std::vector<T> *dst_;
    std::vector<T> values_;
};

template<typename T>
typed_value<T> *value(T *dst = nullptr)
{
    return new typed_value<T>(dst);
}

template<typename T, typename U>
typed_value<T> *value(U *dst)
{
    return new typed_value<T>(dst);
}

inline typed_value<bool> *bool_switch(bool *dst = nullptr)
{
    return value(dst)->default_value(false)->implicit_value(true)->zero_tokens();
}

/* --name sets *dst false; true otherwise. */
inline typed_value<bool> *inverted_bool_switch(bool *dst = nullptr)
{
    return value(dst)->default_value(true)->implicit_value(false)->zero_tokens();
}

class options_description
{
public:
    explicit options_description(std::string title = "") : title_(std::move(title)) {}

    class adder
    {
    public:
        explicit adder(options_description &d) : d_(d) {}
        /* "name" or "name,x" (x: the one-letter form). */
        adder &operator()(const char *names, value_semantic *v, const char *help = "")
        {
            d_.add_option(names, v, help);
            return *this;
        }

    private:
        options_description &d_;
    };

    adder add_options() { return adder(*this); }
    void add(options_description &group) { groups_.push_back(&group); }

    /* Executor's options, parsed out of argv; the rest returned in order. */
    std::vector<std::string> parse(int argc, char **argv);
    void print(std::ostream &out) const;

private:
    struct option
    {
        std::string name, letter, help;
        std::unique_ptr<value_semantic> value;
    };
    void add_option(const char *names, value_semantic *v, const char *help);
    option *find(const std::string &name, bool letter);
    void finish();

    std::string title_;
    std::vector<option> options_;
    std::vector<options_description *> groups_;
};

inline std::ostream &operator<<(std::ostream &out, const options_description &d)
{
    d.print(out);
    return out;
}

}
