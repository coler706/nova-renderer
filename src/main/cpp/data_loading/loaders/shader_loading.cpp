/*!
 * \brief Implements fucntions for loading a shaderpack
 *
 * The functions here work for both zip and folder shaderpacks
 *
 * \author ddubois 
 * \date 03-Sep-16.
 */

#include <easylogging++.h>

#include "loaders.h"
#include "shader_loading.h"
#include "loader_utils.h"
#include "../../utils/utils.h"

namespace nova {
    shaderpack_data load_shaderpack(const std::string &shaderpack_name) {
        // Load the passes
        //  - Check if there's passes in the shaderpack
        //  - If so, identify if there are a complete set of passes
        //      - If there are not, fill in missing passes from the defaults
        //          - If at least one of the pass filenames matches one of the filenames of the default Bedrock
        //              pipelines, load the Bedrock passes as the default passes
        //          - If at least one of the pass filenames matches one of the filenames of the default Optifine
        //              passes, load the Optifine passes as the default passes
        //          - If all filenames are unique, there are no default passes because the passes
        //  - If there are no passes, check the shader names
        //      - If all the shader names match Bedrock shader names, load the Bedrock passes as the default passes
        //      - If all the shader names match Optifine shader names, load the Optifine passes as the default passes
        //      - If some of the shader names match Bedrock shader names, load the Bedrock passes as the default passes
        //          and print a warning (they might be including the files in their other files)
        //      - If some of the shader names match Bedrock shader names, load the Optifine passes as the default passes
        //          and print a warning (they might be including the files in their other files)
        //      - If none of the shader names match known shader names, and there's no passes, thenw e don't know how to
        //          handle this. Print an error, pop up an error on screen about "this shaderpack can't be loaded" and
        //          make the user chose something else

        LOG(INFO) << "Loading shaderpack " << shaderpack_name;
        auto pack = shaderpack_data{};

        if(is_zip_file(shaderpack_name)) {
            LOG(TRACE) << "Loading shaderpack " << shaderpack_name << " from a zip file";

            auto sources = load_sources_from_zip_file(shaderpack_name, {});
            return {};

        } else {
            LOG(TRACE) << "Loading shaderpack " << shaderpack_name << " from a regular folder";

            auto shaderpack_directory = fs::path("shaderpacks") / shaderpack_name;

            LOG(INFO) << "Loading pipelines";
            pack.pipelines_by_pass = load_pipelines_from_folder(shaderpack_directory);
            LOG(INFO) << "Loading passes";
            pack.passes = load_passes_from_folder(shaderpack_directory);
            LOG(INFO) << "Loading dynamic textures";
            pack.dynamic_textures = load_texture_definitions_from_folder(shaderpack_directory);
            LOG(INFO) << "Loaded " << pack.dynamic_textures.size() << " textures";
            LOG(INFO) << "Loading materials";
            pack.materials = load_materials_from_folder(shaderpack_directory);

            if(pack.passes.empty()) {
                LOG(ERROR) << "Shaderpack " << shaderpack_name << " doesn't define any passes, it can't render anything";
            }
            if(pack.pipelines_by_pass.empty()) {
                LOG(ERROR) << "Shaderpack " << shaderpack_name << " doesn't define any pipelines, it can't render anything";
            }
            if(pack.materials.empty()) {
                LOG(ERROR) << "Shaderpack " << shaderpack_name << " doesn't define any materials, it can't render anything";
            }
        }

        LOG(INFO) << "All data for shaderpack " << shaderpack_name << " read from disk";

        return pack;
    }

    template<typename Type>
    void fill_in_pipeline_state_field(const std::string& our_name, std::unordered_map<std::string, pipeline_data>& all_pipelines, std::function<optional<Type>&(pipeline_data&)> get_field_from_pipeline) {
        auto &us = all_pipelines[our_name];
        auto &cur_state = us;
        bool value_found = (bool) get_field_from_pipeline(us);

        while(!value_found) {
            const auto &parent_name = cur_state.parent_name;
            if(parent_name) {
                cur_state = all_pipelines[parent_name.value()];
                auto field_value = get_field_from_pipeline(cur_state);
                if(field_value) {
                    get_field_from_pipeline(us) = field_value.value();
                    value_found = true;
                }

            } else {
                break;
            }
        }
    }

    template<typename Type>
    void fill_field(const std::string& name, std::unordered_map<std::string, pipeline_data> pipelines, optional<Type> pipeline_data::* ptr) {
        fill_in_pipeline_state_field<Type>(name, pipelines, [ptr](pipeline_data& s) -> optional<Type>&{ return s.*ptr; });
    }

    std::vector<pipeline_data> parse_pipelines_from_json(const nlohmann::json &pipelines_json) {
        LOG(DEBUG) << "About to parse " << pipelines_json.size() << " pipelines";
        std::unordered_map<std::string, pipeline_data> definition_map;
        for(auto itr = pipelines_json.begin(); itr != pipelines_json.end(); ++itr) {
            auto pipeline_state_name = itr.key();
            LOG(TRACE) << "Handling pipeline " << pipeline_state_name;
            auto json_node = itr.value();
            optional<std::string> parent_state_name = optional<std::string>{};

            auto colon_pos = pipeline_state_name.find(':');
            if(colon_pos != std::string::npos) {
                auto parent_name = pipeline_state_name.substr(colon_pos + 1);
                parent_state_name = parent_name;
                pipeline_state_name = pipeline_state_name.substr(0, colon_pos);
            }

            auto mat = pipeline_data(pipeline_state_name, parent_state_name, json_node);
            definition_map[pipeline_state_name] = mat;
            LOG(TRACE) << "Inserted a pipeline named " << pipeline_state_name;
        }

        auto pipelines = std::vector<pipeline_data>{};

        // I don't really know the O(n) for this thing. It's at least O(n) and probs O(nlogn) but someone mathy can
        // figure it out
        for(const auto& item : definition_map) {
            auto &cur_state = item.second;

            if(!cur_state.parent_name) {
                // No parent? I guess we get what we have then

                pipelines.push_back(cur_state);
                continue;
            }

            fill_field(item.first, definition_map, &pipeline_data::pass);
            fill_field(item.first, definition_map, &pipeline_data::defines);
            fill_field(item.first, definition_map, &pipeline_data::states);
            fill_field(item.first, definition_map, &pipeline_data::vertex_shader);
            fill_field(item.first, definition_map, &pipeline_data::fragment_shader);
            fill_field(item.first, definition_map, &pipeline_data::geometry_shader);
            fill_field(item.first, definition_map, &pipeline_data::tessellation_evaluation_shader);
            fill_field(item.first, definition_map, &pipeline_data::tessellation_control_shader);
            fill_field(item.first, definition_map, &pipeline_data::vertex_fields);
            fill_field(item.first, definition_map, &pipeline_data::front_face);
            fill_field(item.first, definition_map, &pipeline_data::back_face);
            fill_field(item.first, definition_map, &pipeline_data::input_textures);
            fill_field(item.first, definition_map, &pipeline_data::output_textures);
            fill_field(item.first, definition_map, &pipeline_data::depth_texture);
            fill_field(item.first, definition_map, &pipeline_data::filters);
            fill_field(item.first, definition_map, &pipeline_data::fallback);
            fill_field(item.first, definition_map, &pipeline_data::depth_bias);
            fill_field(item.first, definition_map, &pipeline_data::slope_scaled_depth_bias);
            fill_field(item.first, definition_map, &pipeline_data::stencil_ref);
            fill_field(item.first, definition_map, &pipeline_data::stencil_read_mask);
            fill_field(item.first, definition_map, &pipeline_data::stencil_write_mask);
            fill_field(item.first, definition_map, &pipeline_data::msaa_support);
            fill_field(item.first, definition_map, &pipeline_data::primitive_mode);
            fill_field(item.first, definition_map, &pipeline_data::source_blend_factor);
            fill_field(item.first, definition_map, &pipeline_data::destination_blend_factor);
            fill_field(item.first, definition_map, &pipeline_data::alpha_src);
            fill_field(item.first, definition_map, &pipeline_data::alpha_dst);
            fill_field(item.first, definition_map, &pipeline_data::depth_func);
            fill_field(item.first, definition_map, &pipeline_data::render_queue);

            LOG(TRACE) << "Filled in all fields on pipeline " << cur_state.name;

            pipelines.push_back(cur_state);
        }

        return pipelines;
    }

    std::unordered_map<std::string, render_pass> parse_passes_from_json(const nlohmann::json& json) {
        auto passes = std::unordered_map<std::string, render_pass>{};

        for(const auto& pass_json : json) {
            auto pass = render_pass(pass_json);
            passes[pass.name] = pass;
        }

        return passes;
    }

    std::unordered_map<std::string, texture_resource> parse_textures_from_json(nlohmann::json& json) {
        auto textures = std::unordered_map<std::string, texture_resource>{};

        for(const auto& texture_json : json) {
            LOG(INFO) << "Parsing texture " << texture_json;
            try {
                auto texture = texture_resource(texture_json);
                textures[texture.name] = texture;
            } catch(std::exception& e) {
                LOG(ERROR) << "Could not parse texture description. Reason: " << e.what();
            }
        }

        return textures;
    }

    std::string get_filename_from_include(const std::string& include_line) {
        auto quote_pos = include_line.find('"');
        return include_line.substr(quote_pos + 1, include_line.size() - quote_pos - 2);
    }

    std::unordered_map<std::string, shader_definition> load_sources_from_zip_file(const std::string &shaderpack_name, const std::vector<std::string> &shader_names) {
        LOG(FATAL) << "Cannot load zipped shaderpack " << shaderpack_name;
        throw std::runtime_error("Zipped shaderpacks not yet supported");
    }

    nlohmann::json& get_default_bedrock_passes() {
        static nlohmann::json default_bedrock_passes;

        if(default_bedrock_passes.empty()) {
            std::ifstream default_json_file("config/default/bedrock_passes.json");
            if(default_json_file.is_open()) {
                default_bedrock_passes = load_json_from_stream(default_json_file);
            } else {
                LOG(ERROR) << "Could not open config/default/bedrock_passes.json. Please download it from https://raw.githubusercontent.com/NovaMods/nova-renderer/master/jars/config/shaders.json";
            }
        }

        return default_bedrock_passes;
    }

    nlohmann::json& get_default_optifine_passes() {
        static nlohmann::json default_optifine_passes;

        if(default_optifine_passes.empty()) {
            std::ifstream default_json_file("config/default/bedrock_passes.json");
            if(default_json_file.is_open()) {
                default_optifine_passes = load_json_from_stream(default_json_file);
            } else {
                LOG(ERROR) << "Could not open config/default/bedrock_passes.json. Please download it from https://raw.githubusercontent.com/NovaMods/nova-renderer/master/jars/config/shaders.json";
            }
        }

        return default_optifine_passes;
    }
}
