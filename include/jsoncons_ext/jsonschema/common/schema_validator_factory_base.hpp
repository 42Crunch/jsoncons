// Copyright 2013-2026 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_EXT_JSONSCHEMA_COMMON_SCHEMA_VALIDATOR_FACTORY_BASE_HPP
#define JSONCONS_EXT_JSONSCHEMA_COMMON_SCHEMA_VALIDATOR_FACTORY_BASE_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <jsoncons/nonstd/compiler_support.hpp>
#include <jsoncons/utility/string_utils.hpp>

#include <jsoncons_ext/jsonschema/common/compilation_context.hpp>
#include <jsoncons_ext/jsonschema/common/schema_validator.hpp>
#include <jsoncons_ext/jsonschema/common/schema_readers.hpp>
#include <jsoncons_ext/jsonschema/evaluation_options.hpp>

namespace jsoncons {
namespace jsonschema {

    template <typename Json>
    using resolve_uri_type = std::function<Json(const jsoncons::uri & /*id*/)>;

    template <typename Json>
    class schema_validator_factory_base;

    // External schemas (those loaded through the resolvers) compiled once and
    // shared by the compilations given this store, see
    // make_json_schema(root_schema, resolver, shared_store, options).
    //
    // - It must outlive all the json_schema compiled with it, which reference
    //   its schemas.
    // - The external schemas are identified by their URI, so the resolvers of
    //   all these compilations must return the same schema for the same URI.
    // - The schemas of a root document itself are never shared.
    // - Compilations with the same store must not run concurrently, while the
    //   json_schema compiled with it can validate concurrently as usual.
    template <typename Json>
    class shared_schema_store
    {
        friend class schema_validator_factory_base<Json>;

        // All the (sub)schemas of the shared external schemas by URI
        std::map<jsoncons::uri, schema_validator<Json>*> schemas_;
        // Owns the shared external schemas
        std::vector<std::unique_ptr<schema_validator<Json>>> owned_;
    public:
        shared_schema_store() = default;
        shared_schema_store(const shared_schema_store&) = delete;
        shared_schema_store& operator=(const shared_schema_store&) = delete;

        // Number of external schemas compiled in the store
        std::size_t size() const
        {
            return owned_.size();
        }
    };

    template <typename Json>
    class schema_validator_factory_base
    {
    public:
        using schema_store_type = std::map<jsoncons::uri, schema_validator<Json>*>;
        using validator_factory_factory_type = std::function<std::unique_ptr<schema_validator_factory_base<Json>>(std::unique_ptr<Json>&&,
            const evaluation_options&,schema_store_type*,const std::vector<resolve_uri_type<Json>>&,
            const std::unordered_map<std::string,bool>&)>;
        using schema_validator_ptr_type = typename std::unique_ptr<schema_validator<Json>>;
        using ref_validator_type = ref_validator<Json>;
        using ref_type = ref<Json>;
        using anchor_uri_map_type = std::unordered_map<std::string,uri_wrapper>;

    private:
        std::string spec_version_;
        std::unique_ptr<Json> root_schema_{};
        validator_factory_factory_type factory_factory_;
        evaluation_options options_;
        schema_store_type* schema_store_ptr_;
        std::vector<resolve_uri_type<Json>> resolve_funcs_;
        std::unordered_map<std::string,bool> vocabulary_;

        schema_validator_ptr_type root_;       
        
        // Owns external schemas
        std::vector<schema_validator_ptr_type> schema_validators_;

        // Shared external schemas (optional), and while compiling one of them
        shared_schema_store<Json>* shared_store_{nullptr};
        bool compiling_shared_{false};
        // Refs created while compiling a shared schema (resolved from the
        // shared store only, never from this document's own schemas)
        std::set<const ref_type*> shared_refs_;
        // What this compilation added to the shared store (for rollback)
        std::vector<jsoncons::uri> shared_inserted_;
    protected:
        std::vector<std::pair<jsoncons::uri, ref_type*>> unresolved_refs_; 
        std::map<jsoncons::uri, Json> unknown_keywords_;

    public:

        schema_validator_factory_base(const std::string& version, 
            std::unique_ptr<Json>&& root_schema, 
            const validator_factory_factory_type& factory_factory,
            const evaluation_options& options, 
            schema_store_type* schema_store_ptr,
            const std::vector<resolve_uri_type<Json>>& resolve_funcs)
            : spec_version_(version), root_schema_(std::move(root_schema)), factory_factory_(factory_factory), options_(options),
              schema_store_ptr_(schema_store_ptr), resolve_funcs_(resolve_funcs)
        {
            JSONCONS_ASSERT(schema_store_ptr != nullptr);
        }

        schema_validator_factory_base(const std::string& version, 
            std::unique_ptr<Json>&& root_schema, 
            const validator_factory_factory_type& factory_factory,
            const evaluation_options& options, schema_store_type* schema_store_ptr,
            const std::vector<resolve_uri_type<Json>>& resolve_funcs,
            const std::unordered_map<std::string,bool>& vocabulary)
            : spec_version_(version), root_schema_(std::move(root_schema)), factory_factory_(factory_factory), options_(options),
              schema_store_ptr_(schema_store_ptr), resolve_funcs_(resolve_funcs), vocabulary_(vocabulary)
        {
            JSONCONS_ASSERT(schema_store_ptr != nullptr);
        }

        virtual ~schema_validator_factory_base() = default;

        const std::unordered_map<std::string,bool>& vocabulary() const {return vocabulary_;}
        
        void set_shared_store(shared_schema_store<Json>* shared_store)
        {
            shared_store_ = shared_store;
        }

        void save_schema(schema_validator_ptr_type&& schema)
        {
            if (compiling_shared_)
            {
                shared_store_->owned_.emplace_back(std::move(schema));
            }
            else
            {
                schema_validators_.emplace_back(std::move(schema));
            }
        }

        // Look up a schema by URI in this document's store, then in the
        // shared store (only there if shared_only)
        bool find_schema(const jsoncons::uri& u, schema_validator<Json>*& s,
            bool shared_only = false) const
        {
            if (!shared_only)
            {
                auto it = schema_store_ptr_->find(u);
                if (it != schema_store_ptr_->end())
                {
                    s = (*it).second;
                    return true;
                }
            }
            if (shared_store_ != nullptr)
            {
                auto it = shared_store_->schemas_.find(u);
                if (it != shared_store_->schemas_.end())
                {
                    s = (*it).second;
                    return true;
                }
            }
            return false;
        }

        bool is_shared_ref(const ref_type* r) const
        {
            return shared_refs_.find(r) != shared_refs_.end();
        }

        const std::string& schema() const
        {
            return spec_version_;
        }
        
        void build_schema() 
        {
            anchor_uri_map_type anchor_dict;
            root_ = make_schema_validator(compilation_context<Json>(uri_wrapper(options_.default_base_uri())), *root_schema_, {}, anchor_dict);
        }

        void build_schema(const std::string& retrieval_uri) 
        {
            anchor_uri_map_type anchor_dict;
            root_ = make_schema_validator(compilation_context<Json>(uri_wrapper(retrieval_uri)), *root_schema_, {}, anchor_dict);
        }

        const evaluation_options& options() const
        {
            return options_;
        }
        
        schema_validator_ptr_type make_boolean_schema(const compilation_context<Json>& context, const Json& sch)
        {
            uri schema_location = context.get_base_uri();
            schema_validator_ptr_type schema_validator_ptr = jsoncons::make_unique<boolean_schema_validator<Json>>( 
                schema_location, sch.template as<bool>());

            return schema_validator_ptr;
        }
        
        std::unique_ptr<document_schema_validator<Json>> get_schema_validator()
        {                        
            //std::cout << "schema_store:\n";
            //for (auto& member : *schema_store_ptr_)
            //{
            //    std::cout << "    " << member.first.string() << "\n";
            //}

            // On failure, remove what was added to the shared store (whose
            // refs may not be resolved), as if this compilation never happened
            std::size_t shared_owned_mark = shared_store_ ? shared_store_->owned_.size() : 0;
            JSONCONS_TRY
            {
                load_external_schemas();
                resolve_references();
            }
            JSONCONS_CATCH (...)
            {
                compiling_shared_ = false;
                if (shared_store_ != nullptr)
                {
                    for (const auto& u : shared_inserted_)
                    {
                        shared_store_->schemas_.erase(u);
                    }
                    while (shared_store_->owned_.size() > shared_owned_mark)
                    {
                        shared_store_->owned_.pop_back();
                    }
                }
                JSONCONS_RETHROW;
            }

            return jsoncons::make_unique<document_schema_validator<Json>>(std::move(root_schema_), std::move(root_), std::move(schema_validators_));
        }

        void load_external_schemas()
        {
            // load all external schemas that have not already been loaded
            // new unresolved refs may be added to the end as earlier ones are resolved
            for (std::size_t i = 0; i < unresolved_refs_.size(); ++i)
            {
                auto loc = unresolved_refs_[i].first;
                bool shared_only = is_shared_ref(unresolved_refs_[i].second);
                schema_validator<Json>* s = nullptr;
                //std::cout << "unresolved: " << loc.string() << "\n";
                if (!find_schema(loc, s, shared_only)) // registry for this file is empty
                {
                    bool found = false;
                    for (auto it = resolve_funcs_.begin(); it != resolve_funcs_.end() && !found; ++it)
                    {
                        Json external_sch = (*it)(loc);

                        if (external_sch.is_object() || external_sch.is_bool())
                        {
                            anchor_uri_map_type anchor_dict2;
                            // Compile it in the shared store if any
                            compiling_shared_ = (shared_store_ != nullptr);
                            auto external = make_cross_draft_schema_validator(compilation_context<Json>(uri_wrapper(loc.base())),
                                external_sch, {}, anchor_dict2);
                            this->save_schema(std::move(external));
                            compiling_shared_ = false;
                            found = true;
                        }
                    }
                    if (found)
                    {
                        // Try resolving again
                        if (!find_schema(loc, s, shared_only))
                        {
                            JSONCONS_THROW(jsonschema::schema_error("Unresolved reference '" + loc.string() + "'"));
                        }
                    }
                    else
                    {
                        JSONCONS_THROW(jsonschema::schema_error("Don't know how to load JSON Schema '" + loc.string() + "'" ));
                    }
                }
            }
        }

        void resolve_references()
        {
            for (auto& ref : unresolved_refs_)
            {
                schema_validator<Json>* s = nullptr;
                if (!find_schema(ref.first, s, is_shared_ref(ref.second)))
                {
                    JSONCONS_THROW(schema_error("Undefined reference " + ref.first.string()));
                }
                if (s == nullptr)
                {
                    JSONCONS_THROW(schema_error("Null referred schema " + ref.first.string()));
                }
                ref.second->set_referred_schema(s);
            }
        }

        void insert_schema(const uri_wrapper& identifier, schema_validator<Json>* s)
        {
            if (compiling_shared_)
            {
                if (shared_store_->schemas_.emplace(identifier.uri(), s).second)
                {
                    shared_inserted_.push_back(identifier.uri());
                }
            }
            else
            {
                this->schema_store_ptr_->emplace(identifier.uri(), s);
            }
        }

        void insert_unknown_keyword(const uri_wrapper& uri, 
                                    const std::string& key, 
                                    const Json& value)
        {
            auto new_u = uri.append(key);
            uri_wrapper new_uri(new_u);

            if (new_uri.has_fragment() && !new_uri.has_plain_name_fragment()) 
            {
                // is there a reference looking for this unknown-keyword, which is thus no longer a unknown keyword but a schema
                auto unresolved_refs = std::find_if(this->unresolved_refs_.begin(), this->unresolved_refs_.end(),
                    [new_uri](const std::pair<jsoncons::uri,ref<Json>*>& pr) {return pr.first == new_uri.uri();});
                if (unresolved_refs != this->unresolved_refs_.end())
                {
                    anchor_uri_map_type anchor_dict2;
                    this->save_schema(make_cross_draft_schema_validator(compilation_context<Json>(new_uri), value, {}, anchor_dict2));
                }
                else // no, nothing ref'd it, keep for later
                {
                    //file.unknown_keywords.emplace(fragment, value);
                    this->unknown_keywords_.emplace(new_uri.uri(), value);
                }

                // recursively add possible subschemas of unknown keywords
                if (value.type() == json_type::object)
                {
                    for (const auto& subsch : value.object_range())
                    {
                        insert_unknown_keyword(new_uri, std::string(subsch.name()), subsch.value());
                    }
                }
            }
        }

        std::unique_ptr<ref_validator<Json>> get_or_create_reference(const Json& schema, const uri_wrapper& identifier)
        {
            // a schema already exists (only shared ones for a shared schema)
            schema_validator<Json>* s = nullptr;
            if (find_schema(identifier.uri(), s, compiling_shared_))
            {
                return jsoncons::make_unique<ref_validator_type>(schema, identifier.uri(), s);
            }

            // referencing an unknown keyword, turn it into schema
            //
            // an unknown keyword can only be referenced by a JSONPointer,
            // not by a plain name identifier
            if (identifier.has_fragment() && !identifier.has_plain_name_fragment()) 
            {
                //std::string fragment = std::string(identifier.fragment());

                auto it2 = this->unknown_keywords_.find(identifier.uri());
                if (it2 != this->unknown_keywords_.end())
                {
                    auto& subsch = it2->second;
                    anchor_uri_map_type anchor_dict2;
                    auto s = make_cross_draft_schema_validator(compilation_context<Json>(identifier), subsch, {}, anchor_dict2);
                    this->unknown_keywords_.erase(it2);
                    auto orig = jsoncons::make_unique<ref_validator_type>(schema, identifier.uri(), s.get());
                    this->save_schema(std::move(s));
                    return orig;
                }
            }

            // get or create a ref_validator
            auto orig = jsoncons::make_unique<ref_validator_type>(schema, identifier.uri());

            this->unresolved_refs_.emplace_back(identifier.uri(), orig.get());
            if (compiling_shared_)
            {
                shared_refs_.insert(orig.get());
            }
            return orig;
        }

        virtual compilation_context<Json> make_compilation_context(const compilation_context<Json>& parent,
            const Json& sch, jsoncons::span<const jsoncons::string_view> keys) const = 0;

        virtual schema_validator_ptr_type make_schema_validator(const compilation_context<Json>& context, 
            const Json& sch, jsoncons::span<const jsoncons::string_view> keys, anchor_uri_map_type& anchor_dict) = 0;

        schema_validator_ptr_type make_cross_draft_schema_validator(const compilation_context<Json>& context, 
            const Json& sch, jsoncons::span<const jsoncons::string_view> keys, anchor_uri_map_type& anchor_dict)
        {
            schema_validator_ptr_type schema_val = schema_validator_ptr_type{};
            switch (sch.type())
            {
                case json_type::object:
                {
                    auto it = sch.find("$schema");
                    if (it != sch.object_range().end())
                    {
                        if ((*it).value().as_string_view() == schema())
                        {
                            return make_schema_validator(context, sch, keys, anchor_dict);
                        }
                        else
                        {
                            auto schema_validator_factory_base = factory_factory_(jsoncons::make_unique<Json>(sch), options_, schema_store_ptr_, resolve_funcs_, vocabulary_);
                            schema_validator_factory_base->build_schema(context.get_base_uri().string());
                            schema_val = schema_validator_factory_base->get_schema_validator();
                        }
                    }
                    else
                    {
                        return make_schema_validator(context, sch, keys, anchor_dict);
                    }
                    break;
                }
                case json_type::boolean:
                {
                    return make_schema_validator(context, sch, keys, anchor_dict);
                }
                default:
                    JSONCONS_THROW(schema_error("Schema must be object or boolean"));
            }
            return schema_val;
        }

        static void read_id_201909_latest(const compilation_context<Json>& parent, const Json& sch, 
            jsoncons::optional<uri>& id,
            std::vector<uri_wrapper>& new_uris)
        {
            auto it = sch.find("$id"); // If $id is found, this schema can be referenced by the id
            if (it != sch.object_range().end()) 
            {
                if (!(*it).value().is_string())
                {
                    JSONCONS_THROW(schema_error("$id must be string"));
                }
                auto sv = (*it).value().as_string_view();
                uri relative(sv); 
                if (relative.has_fragment())
                {
                    std::string message{sv};
                    message.append(": Drafts 2019-09 and later do not allow $id with fragment");
                    JSONCONS_THROW(schema_error(message));
                }
                auto resolved = parent.get_base_uri().resolve(relative);
                id = resolved;
                uri_wrapper new_uri{resolved};
                //std::cout << "$id: " << id << ", " << new_uri.string() << "\n";
                // Add it to the list if it is not already there
                if (std::find(new_uris.begin(), new_uris.end(), new_uri) == new_uris.end())
                {
                    new_uris.emplace_back(new_uri); 
                }
            }
        }
    };

} // namespace jsonschema
} // namespace jsoncons

#endif // JSONCONS_EXT_JSONSCHEMA_COMMON_SCHEMA_VALIDATOR_FACTORY_BASE_HPP
