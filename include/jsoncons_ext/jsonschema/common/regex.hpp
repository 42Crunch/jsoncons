// Copyright 2013-2025 Daniel Parker
// 
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_EXT_JSONSCHEMA_COMMON_REGEX_HPP
#define JSONCONS_EXT_JSONSCHEMA_COMMON_REGEX_HPP

#if defined(JSONCONS_JSONSCHEMA_HAS_PCRX)
#include <pcrx.h>
#define JSONCONS_JSONSCHEMA_HAS_REGEX 1
#elif defined(JSONCONS_JSONSCHEMA_HAS_PCRE2)
#include <pcre2.h>
#define JSONCONS_JSONSCHEMA_HAS_REGEX 1
#elif defined(JSONCONS_HAS_STD_REGEX)
#include <regex>
#define JSONCONS_JSONSCHEMA_HAS_REGEX 1
#endif

#include <jsoncons/config/jsoncons_config.hpp> // jsoncons::basic_string_view

namespace jsoncons {
namespace jsonschema {
    
    template<class CharT>
    class basic_regex
    {
    private:
#if defined(JSONCONS_JSONSCHEMA_HAS_PCRX)
        pcrx_t re_;
#elif defined(JSONCONS_JSONSCHEMA_HAS_PCRE2)
        pcre2_code *re_;
#elif defined(JSONCONS_HAS_STD_REGEX)
        std::regex re_;
#endif
        std::basic_string<CharT> pattern_;
        std::string error_str_;
        int compiled_;

    public:
        using char_type = CharT;
        using string_type = std::basic_string<char_type>;

        basic_regex() :
            re_(),
            compiled_()
        { }

        basic_regex(const basic_regex&) = delete;
        basic_regex &operator=(const basic_regex&) = delete;

        basic_regex(basic_regex &&other) :
            re_(),
            pattern_(std::move(other.pattern_)),
            error_str_(std::move(other.error_str_)),
            compiled_(other.compiled_)
        {
            std::swap(re_, other.re_);
        }
        basic_regex &operator=(basic_regex &&other)
        {
            basic_regex tmp(std::move(re_)); // RAII destroyed
            pattern_ = std::move(other.pattern_);
            error_str_ = std::move(other.error_str_);
            std::swap(re_, other.re_);
            return *this;
        }

        basic_regex(const std::basic_string<CharT> &pattern) :
            re_(),
            pattern_(pattern),
            compiled_()
        { }

        basic_regex(jsoncons::basic_string_view<CharT> pattern) :
            re_(),
            pattern_(pattern.data(), pattern.size()),
            compiled_()
        { }

        basic_regex(const CharT *pattern, size_t length) :
            re_(),
            pattern_(pattern, length),
            compiled_()
        { }

        ~basic_regex()
        {
#if defined(JSONCONS_JSONSCHEMA_HAS_PCRX)
            pcrx_clear(&re_);
#elif defined(JSONCONS_JSONSCHEMA_HAS_PCRE2)
            if (re_) {
                pcre2_code_free(re_);
            }
#endif
        }

        bool compile()
        {
            if (compiled_) {
                return compiled_ > 0;
            }

#if defined(JSONCONS_JSONSCHEMA_HAS_PCRX)

            if (!pcrx_compile(&re_, pattern_.data(), pattern_.length(),
                                 PCRX(ALT_BSUX) | PCRX(UTF) | PCRX(DOTALL))) {
                error_str_ = "error " + std::to_string(re_.error_code) + " at offset " + std::to_string(re_.error_offset);
                return false;
            }

#elif defined(JSONCONS_JSONSCHEMA_HAS_PCRE2)

            int err_code = 0;
            size_t err_off = 0;
            re_ = pcre2_compile(pattern, length,
                                PCRE2_ALT_BSUX | PCRE2_UTF8 | PCRE2_DOTALL | PCRE2_DOLLAR_ENDONLY,
                                &err_code, &err_offset, NULL);
            if (!re_) {
                error_str_ = "error " + std::to_string(err_code) " at offset " + std::to_string(err_off);
                compiled_ = -1;
                return false;
            }

#elif defined(JSONCONS_HAS_STD_REGEX)

            try {
                re_.assign(pattern_, std::regex::ECMAScript);
            }
            catch (const std::exception& e) {
                error_str_ = e.what();
                compiled_ = -1;
                return false;
            }

#endif

            compiled_ = 1;
            return true;
        }

        bool search(jsoncons::basic_string_view<CharT> subject) const
        {
            if (compiled_ <= 0) {
                return false;

            }

            /* The subject's UTF-8 is checked (not PCRE2_NO_UTF_CHECK, undefined
             * behaviour for an invalid one, which can come from any string not
             * parsed from JSON), an invalid one doesn't match.
             */
#if defined(JSONCONS_JSONSCHEMA_HAS_PCRX)

            if (pcrx_match_ex(&re_, 0,
                              subject.data(), subject.size(), 0,
                              NULL, 0) < 0) {
                return false;
            }

#elif defined(JSONCONS_JSONSCHEMA_HAS_PCRE2)

            pcre2_match_data *match_data;
            match_data = pcre2_match_data_create_from_pattern(re_, NULL);
            JSONCONS_ASSERT(match_data != nullptr);
            int res = pcre2_match(re_, subject.data(), subject.size(), 0,
                                  0, match_data, NULL);
            pcre2_match_data_free(match_data);
            if (res < 0) {
                return false;
            }

#elif defined(JSONCONS_HAS_STD_REGEX)

            if (!std::regex_search(subject.cbegin(), subject.cend(), re_)) {
                return false;
            }

#endif

            return true;
        }

        bool is_valid() const
        {
            return compiled_ > 0;
        }

        const std::string &error() const
        {
            return error_str_;
        }
    };

    using regex = basic_regex<char>;

} // namespace jsonschema
} // namespace jsoncons

#endif // JSONCONS_EXT_JSONSCHEMA_COMMON_REGEX_HPP
