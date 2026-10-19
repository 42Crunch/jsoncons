// Copyright 2013-2026 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

#include <catch/catch.hpp>

#include <map>
#include <string>

using jsoncons::json;
namespace jsonschema = jsoncons::jsonschema;

namespace {

    const json common = json::parse(R"(
{
    "$id": "https://example.com/common.json",
    "$defs": {
        "positive": {"type": "integer", "minimum": 1},
        "pair": {"type": "array", "items": {"$ref": "#/$defs/positive"}, "minItems": 2},
        "tree": {
            "type": "object",
            "properties": {
                "value": {"$ref": "#/$defs/positive"},
                "children": {"type": "array", "items": {"$ref": "#/$defs/tree"}}
            }
        }
    }
}
    )");

    // Serves common.json and counts the calls per document
    struct counting_resolver
    {
        std::map<std::string, int>* calls;

        json operator()(const jsoncons::uri& u) const
        {
            ++(*calls)[u.base().string()];
            if (u.base().string() == "https://example.com/common.json")
            {
                return common;
            }
            return json::null();
        }
    };

} // namespace

TEST_CASE("shared_schema_store: external schemas are compiled once")
{
    std::map<std::string, int> calls;
    counting_resolver resolver{&calls};
    jsonschema::shared_schema_store<json> store;

    json s1 = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                              "$ref": "https://example.com/common.json#/$defs/positive"})");
    json s2 = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                              "type": "object",
                              "properties": {"p": {"$ref": "https://example.com/common.json#/$defs/pair"}}})");

    auto schema1 = jsonschema::make_json_schema(s1, resolver, store);
    CHECK(store.size() == 1);

    auto schema2 = jsonschema::make_json_schema(s2, resolver, store);
    CHECK(calls["https://example.com/common.json"] == 1);
    CHECK(store.size() == 1);

    CHECK(schema1.is_valid(json(1)));
    CHECK_FALSE(schema1.is_valid(json(0)));
    CHECK(schema2.is_valid(json::parse(R"({"p": [1, 2]})")));
    CHECK_FALSE(schema2.is_valid(json::parse(R"({"p": [1, 0]})")));
    CHECK_FALSE(schema2.is_valid(json::parse(R"({"p": [1]})")));
}

TEST_CASE("shared_schema_store: recursive external schema")
{
    std::map<std::string, int> calls;
    counting_resolver resolver{&calls};
    jsonschema::shared_schema_store<json> store;

    json s = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                             "$ref": "https://example.com/common.json#/$defs/tree"})");
    auto schema1 = jsonschema::make_json_schema(s, resolver, store);
    auto schema2 = jsonschema::make_json_schema(s, resolver, store);
    CHECK(calls["https://example.com/common.json"] == 1);

    json good = json::parse(R"({"value": 1, "children": [{"value": 2, "children": [{"value": 3}]}]})");
    json bad = json::parse(R"({"value": 1, "children": [{"value": 2, "children": [{"value": 0}]}]})");
    CHECK(schema1.is_valid(good));
    CHECK(schema2.is_valid(good));
    CHECK_FALSE(schema1.is_valid(bad));
    CHECK_FALSE(schema2.is_valid(bad));
}

TEST_CASE("shared_schema_store: root schemas are not shared")
{
    std::map<std::string, int> calls;
    counting_resolver resolver{&calls};
    jsonschema::shared_schema_store<json> store;

    // Same (default) base URI and same "$defs" name, different content
    json s1 = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                              "$defs": {"x": {"type": "string"}}, "$ref": "#/$defs/x"})");
    json s2 = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                              "$defs": {"x": {"type": "integer"}}, "$ref": "#/$defs/x"})");
    auto schema1 = jsonschema::make_json_schema(s1, resolver, store);
    auto schema2 = jsonschema::make_json_schema(s2, resolver, store);
    CHECK(store.size() == 0);

    CHECK(schema1.is_valid(json("a")));
    CHECK_FALSE(schema1.is_valid(json(1)));
    CHECK(schema2.is_valid(json(1)));
    CHECK_FALSE(schema2.is_valid(json("a")));
}

TEST_CASE("shared_schema_store: refs of a shared schema don't resolve to the root's")
{
    std::map<std::string, int> calls;
    counting_resolver resolver{&calls};
    jsonschema::shared_schema_store<json> store;

    // The root's own "#/$defs/positive" must not be used by the shared "pair",
    // compiled first with this root
    json s = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                             "$id": "https://example.com/common.json",
                             "$defs": {"positive": {"type": "string"}},
                             "properties": {"p": {"$ref": "https://example.com/common.json#/$defs/pair"}}})");
    json s2 = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                              "$ref": "https://example.com/common.json#/$defs/pair"})");
    // The root itself (and its "#/$defs/pair") has the URI of common.json,
    // so it's not an external schema here
    auto schema = jsonschema::make_json_schema(s, resolver, store);
    CHECK_FALSE(schema.is_valid(json::parse(R"({"p": 1})")));

    auto schema2 = jsonschema::make_json_schema(s2, resolver, store);
    CHECK(schema2.is_valid(json::parse("[1, 2]")));
    CHECK_FALSE(schema2.is_valid(json::parse(R"(["a", "b"])")));
}

TEST_CASE("shared_schema_store: a failing compilation leaves the store unchanged")
{
    std::map<std::string, int> calls;
    counting_resolver resolver{&calls};
    jsonschema::shared_schema_store<json> store;

    json ok = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                              "$ref": "https://example.com/common.json#/$defs/positive"})");
    auto schema1 = jsonschema::make_json_schema(ok, resolver, store);
    std::size_t size = store.size();

    // other.json is broken first ("$ref" to a missing document), then fixed
    bool broken = true;
    auto other_resolver = [&](const jsoncons::uri& u) -> json
    {
        if (u.base().string() == "https://example.com/other.json")
        {
            ++calls[u.base().string()];
            return json::parse(broken
                ? R"({"$id": "https://example.com/other.json",
                      "$defs": {"y": {"$ref": "https://example.com/missing.json"}}})"
                : R"({"$id": "https://example.com/other.json",
                      "$defs": {"y": {"type": "string"}}})");
        }
        return resolver(u);
    };
    json ko = json::parse(R"({"$schema": "https://json-schema.org/draft/2020-12/schema",
                              "$ref": "https://example.com/other.json#/$defs/y"})");
    REQUIRE_THROWS_AS(jsonschema::make_json_schema(ko, other_resolver, store), jsonschema::schema_error);
    CHECK(store.size() == size);
    CHECK(calls["https://example.com/other.json"] == 1);

    // other.json was removed from the store, so it's loaded again
    broken = false;
    auto schema2 = jsonschema::make_json_schema(ko, other_resolver, store);
    CHECK(calls["https://example.com/other.json"] == 2);
    CHECK(store.size() == size + 1);
    CHECK(schema2.is_valid(json("a")));
    CHECK_FALSE(schema2.is_valid(json(1)));

    CHECK(schema1.is_valid(json(1)));
    CHECK_FALSE(schema1.is_valid(json(0)));
}
