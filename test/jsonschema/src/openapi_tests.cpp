// Copyright 2013-2026 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

// The OpenAPI extensions of the 42Crunch fork.

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

#include <catch/catch.hpp>

#include <string>
#include <vector>

using jsoncons::json;
namespace jsonschema = jsoncons::jsonschema;

namespace {

    jsonschema::json_schema<json> compile(const std::string& schema,
        jsonschema::evaluation_options options = jsonschema::evaluation_options{})
    {
        return jsonschema::make_json_schema(json::parse(schema),
            options.default_version(jsonschema::schema_version::draft7()));
    }

    // The messages of a validation (all of them)
    std::vector<std::string> errors(const jsonschema::json_schema<json>& schema, const std::string& instance)
    {
        std::vector<std::string> messages;
        auto reporter = [&](const jsonschema::validation_message& msg) -> jsonschema::walk_result
        {
            messages.push_back(msg.message());
            return jsonschema::walk_result::advance;
        };
        schema.validate(json::parse(instance), reporter);
        return messages;
    }

} // namespace

TEST_CASE("openapi nullable")
{
    SECTION("adds null to a single type, the other keywords still apply")
    {
        auto schema = compile(R"({"type": "string", "nullable": true, "maxLength": 2})");
        CHECK(errors(schema, R"(null)").empty());
        CHECK(errors(schema, R"("ab")").empty());
        CHECK(errors(schema, R"("abc")").size() == 1);
        CHECK(errors(schema, R"(1)").size() == 1);

        auto xschema = compile(R"({"type": "integer", "x-nullable": true})");
        CHECK(errors(xschema, R"(null)").empty());
        CHECK(errors(xschema, R"(1)").empty());
    }
    SECTION("the strict OAS 3.0.3 reading: an enum or an allOf without null still refuses it")
    {
        auto schema = compile(R"({"type": "string", "nullable": true, "enum": ["a", "b"]})");
        CHECK(errors(schema, R"("a")").empty());
        CHECK(!errors(schema, R"(null)").empty());

        auto composed = compile(R"({"nullable": true, "allOf": [{"type": "string"}]})");
        CHECK(!errors(composed, R"(null)").empty());
    }
    SECTION("false or absent: no null")
    {
        CHECK(!errors(compile(R"({"type": "string", "nullable": false})"), R"(null)").empty());
        CHECK(!errors(compile(R"({"type": "string"})"), R"(null)").empty());
    }
    SECTION("a non-boolean nullable is ignored, with a warning")
    {
        std::vector<std::string> warnings;
        auto options = jsonschema::evaluation_options{}
            .warning_handler([&](const std::string& w) { warnings.push_back(w); });
        jsonschema::json_schema<json> schema = compile(R"({"type": "string", "nullable": "yes"})", options);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].find("\"nullable\" is not a boolean, ignored") != std::string::npos);
        CHECK(!errors(schema, R"(null)").empty());
        CHECK(errors(schema, R"("a")").empty());

        // without a handler, quietly
        CHECK(errors(compile(R"({"type": "string", "x-nullable": 1})"), R"("a")").empty());
    }
}
