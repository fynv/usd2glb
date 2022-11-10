#define NOMINMAX 
#include <cstdio>
#include <tinyusdz.hh>
#include <usdShade.hh>
#include <usdSkel.hh>
#include <usda-writer.hh>

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION


#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <gtx/matrix_decompose.hpp>
#include <vector>
#include <queue>
#include <unordered_map>
#include <filesystem>
#include <crc64.h>

#include "Image.h"

namespace Mid
{
	struct Material
	{
		std::string name;
		bool double_sided = false;
		bool useSpecularWorkflow = false;

		glm::vec3 diffuse_color = glm::vec3(1.0f, 1.0f, 1.0f);
		std::string diffuse_tex = "";
		std::string diffuse_varname = "";

		glm::vec3 emissive_color = glm::vec3(0.0f, 0.0f, 0.0f);
		std::string emissive_tex = "";

		glm::vec3 specular_color = glm::vec3(1.0f, 1.0f, 1.0f);
		std::string specular_tex = "";

		float metallic = 0.0f;
		std::string metallic_tex = "";

		float roughness = 0.5f;
		std::string roughness_tex = "";

		float opacity = 1.0f;
		std::string opacity_tex = "";

		std::string uvset = "";

		int idx_diffuse_alpha = -1;
		int idx_emissive = -1;
		int idx_metallic_roughness = -1;
		int idx_specular_glossiness = -1;
	};
}

#if 1
int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		printf("usd2glb input.usdc output.glb\n");
		return 0;
	}

	std::string path_model = std::filesystem::path(argv[1]).parent_path().u8string();	

	std::string warn;
	std::string err;

	tinyusdz::Stage stage;
	tinyusdz::USDLoadOptions options;
	options.load_assets = false;
	bool ret = tinyusdz::LoadUSDCFromFile(argv[1], &stage, &warn, &err, options);
	if (!ret)
	{
		printf("%s\n", warn.c_str());
		printf("%s\n", err.c_str());
		return 0;
	}

	// tinyusdz::usda::SaveAsUSDA("output.usda", stage, &warn, &err);
	
	double time_codes_per_sec = stage.metas().timeCodesPerSecond.get_value();
	auto upAxis = stage.metas().upAxis.get_value();

	glm::quat axis_rot = glm::identity<glm::quat>();
	if (upAxis == tinyusdz::Axis::X)
	{
		glm::mat4 rot = { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		axis_rot = rot;
	}
	else if (upAxis == tinyusdz::Axis::Z)
	{
		glm::mat4 rot = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		axis_rot = rot;
	}
	
	tinyusdz::Prim* root_prim = &stage.root_prims()[0];
	
	tinygltf::Model m_out;	
	m_out.scenes.resize(1);
	tinygltf::Scene& scene_out = m_out.scenes[0];
	scene_out.name = "Scene";

	m_out.asset.version = "2.0";
	m_out.asset.generator = "tinygltf";

	m_out.buffers.resize(1);
	tinygltf::Buffer& buf_out = m_out.buffers[0];

	size_t offset = 0;
	size_t length = 0;
	size_t view_id = 0;
	size_t acc_id = 0;

	std::vector<Mid::Material> material_lst;
	std::unordered_map<std::string, int> material_map;

	struct Prim
	{
		tinyusdz::Prim* prim;
		int id_node_base = -1;
		std::string base_path;
		int idx_material = -1;
		std::string skel_path;
	};

	std::queue<Prim> queue_prim;

	bool specular_used = false;

	queue_prim.push({ root_prim, -1, "" });
	while (!queue_prim.empty())
	{
		Prim prim = queue_prim.front();
		queue_prim.pop();
		std::string path = prim.base_path + "/" + prim.prim->element_path().full_path_name();

		if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_MATERIAL)
		{
			Mid::Material material_mid;

			auto* material_in = prim.prim->data().as<tinyusdz::Material>();
			material_mid.name = material_in->name;
			std::string outputs_surface = material_in->surface->target.value().prim_part();
			const tinyusdz::Prim* pshader0 = stage.GetPrimAtPath(tinyusdz::Path(outputs_surface, "")).value();
			auto* shader0 = pshader0->data().as<tinyusdz::Shader>();
			auto* surface = shader0->value.as<tinyusdz::UsdPreviewSurface>();

			int useSpecularWorkflow;
			surface->useSpecularWorkflow.get_value().get_scalar(&useSpecularWorkflow);			
			material_mid.useSpecularWorkflow = useSpecularWorkflow != 0;		

			specular_used = specular_used || material_mid.useSpecularWorkflow;

			{
				auto diffuse = surface->diffuseColor;
				auto diffuse_connection = diffuse.get_connection();

				if (diffuse_connection.has_value())
				{
					std::string path = diffuse_connection.value().prim_part();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					if (shader1->value.type_id() == tinyusdz::value::TYPE_ID_IMAGING_UVTEXTURE)
					{
						auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
						tinyusdz::value::AssetPath file;
						tex->file.get_value().value().get_scalar(&file);
						material_mid.diffuse_tex = file.GetAssetPath();

						auto uv_connection = tex->st.get_connection();
						if (uv_connection.has_value())
						{
							std::string path_uv = uv_connection.value().prim_part();
							const tinyusdz::Prim* pshader2 = stage.GetPrimAtPath(tinyusdz::Path(path_uv, "")).value();
							auto* shader2 = pshader2->data().as<tinyusdz::Shader>();
							auto* uvset = shader2->value.as<tinyusdz::UsdPrimvarReader_float2>();
							tinyusdz::value::token token_uv;
							uvset->varname.get_value().value().get_scalar(&token_uv);
							material_mid.uvset = token_uv.str();
						}
					}
					else if (shader1->value.type_id() == tinyusdz::value::TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT3)
					{
						auto* reader = shader1->value.as<tinyusdz::UsdPrimvarReader_float3>();						
						tinyusdz::value::token varname;
						reader->varname.get_value().value().get_scalar(&varname);
						material_mid.diffuse_varname = varname.str();
					}
					material_mid.diffuse_color = { 1.0f, 1.0f, 1.0f };
				}
				else
				{
					tinyusdz::value::color3f col;
					diffuse.get_value().get_scalar(&col);
					material_mid.diffuse_color = { col[0], col[1], col[2] };
				}

			}

			{
				auto emissive = surface->emissiveColor;
				auto emissive_connection = emissive.get_connection();
				if (emissive_connection.has_value())
				{
					std::string path = emissive_connection.value().prim_part();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					tinyusdz::value::AssetPath file;
					tex->file.get_value().value().get_scalar(&file);
					material_mid.emissive_tex = file.GetAssetPath();

					auto uv_connection = tex->st.get_connection();
					if (uv_connection.has_value())
					{
						std::string path_uv = uv_connection.value().prim_part();
						const tinyusdz::Prim* pshader2 = stage.GetPrimAtPath(tinyusdz::Path(path_uv, "")).value();
						auto* shader2 = pshader2->data().as<tinyusdz::Shader>();
						auto* uvset = shader2->value.as<tinyusdz::UsdPrimvarReader_float2>();
						tinyusdz::value::token token_uv;
						uvset->varname.get_value().value().get_scalar(&token_uv);
						material_mid.uvset = token_uv.str();
					}
					material_mid.emissive_color = { 1.0f, 1.0f, 1.0f };
				}
				else
				{
					tinyusdz::value::color3f col;
					emissive.get_value().get_scalar(&col);
					material_mid.emissive_color = { col[0], col[1], col[2] };
				}
			}

			if (material_mid.useSpecularWorkflow)
			{
				auto specular = surface->specularColor;
				auto specular_connection = specular.get_connection();
				if (specular_connection.has_value())
				{
					std::string path = specular_connection.value().prim_part();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					tinyusdz::value::AssetPath file;
					tex->file.get_value().value().get_scalar(&file);
					material_mid.specular_tex = file.GetAssetPath();

					auto uv_connection = tex->st.get_connection();
					if (uv_connection.has_value())
					{
						std::string path_uv = uv_connection.value().prim_part();
						const tinyusdz::Prim* pshader2 = stage.GetPrimAtPath(tinyusdz::Path(path_uv, "")).value();
						auto* shader2 = pshader2->data().as<tinyusdz::Shader>();
						auto* uvset = shader2->value.as<tinyusdz::UsdPrimvarReader_float2>();
						tinyusdz::value::token token_uv;
						uvset->varname.get_value().value().get_scalar(&token_uv);
						material_mid.uvset = token_uv.str();
					}
					material_mid.specular_color = { 1.0f, 1.0f, 1.0f };
				}			
				else
				{
					tinyusdz::value::color3f col;
					specular.get_value().get_scalar(&col);
					material_mid.specular_color = { col[0], col[1], col[2] };
				}
			}
			else
			{
				auto metallic = surface->metallic;
				auto metallic_connection = metallic.get_connection();
				if (metallic_connection.has_value())
				{
					std::string path = metallic_connection.value().prim_part();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					tinyusdz::value::AssetPath file;
					tex->file.get_value().value().get_scalar(&file);
					material_mid.metallic_tex = file.GetAssetPath();
					material_mid.metallic = 1.0f;

					auto uv_connection = tex->st.get_connection();
					if (uv_connection.has_value())
					{
						std::string path_uv = uv_connection.value().prim_part();
						const tinyusdz::Prim* pshader2 = stage.GetPrimAtPath(tinyusdz::Path(path_uv, "")).value();
						auto* shader2 = pshader2->data().as<tinyusdz::Shader>();
						auto* uvset = shader2->value.as<tinyusdz::UsdPrimvarReader_float2>();
						tinyusdz::value::token token_uv;
						uvset->varname.get_value().value().get_scalar(&token_uv);
						material_mid.uvset = token_uv.str();
					}
				}
				else
				{
					metallic.get_value().get_scalar(&material_mid.metallic);
				}
			}
			{
				auto roughness = surface->roughness;
				auto roughness_connection = roughness.get_connection();
				if (roughness_connection.has_value())
				{
					std::string path = roughness_connection.value().prim_part();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					tinyusdz::value::AssetPath file;
					tex->file.get_value().value().get_scalar(&file);
					material_mid.roughness_tex = file.GetAssetPath();
					material_mid.roughness = 1.0f;

					auto uv_connection = tex->st.get_connection();
					if (uv_connection.has_value())
					{
						std::string path_uv = uv_connection.value().prim_part();
						const tinyusdz::Prim* pshader2 = stage.GetPrimAtPath(tinyusdz::Path(path_uv, "")).value();
						auto* shader2 = pshader2->data().as<tinyusdz::Shader>();
						auto* uvset = shader2->value.as<tinyusdz::UsdPrimvarReader_float2>();
						tinyusdz::value::token token_uv;
						uvset->varname.get_value().value().get_scalar(&token_uv);
						material_mid.uvset = token_uv.str();
					}
				}
				else
				{
					roughness.get_value().get_scalar(&material_mid.roughness);
				}
			}
			{
				auto opacity = surface->opacity;
				auto opacity_connection = opacity.get_connection();
				if (opacity_connection.has_value())
				{
					std::string path = opacity_connection.value().prim_part();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					tinyusdz::value::AssetPath file;
					tex->file.get_value().value().get_scalar(&file);
					material_mid.opacity_tex = file.GetAssetPath();
					material_mid.opacity = 1.0f;

					auto uv_connection = tex->st.get_connection();
					if (uv_connection.has_value())
					{
						std::string path_uv = uv_connection.value().prim_part();
						const tinyusdz::Prim* pshader2 = stage.GetPrimAtPath(tinyusdz::Path(path_uv, "")).value();
						auto* shader2 = pshader2->data().as<tinyusdz::Shader>();
						auto* uvset = shader2->value.as<tinyusdz::UsdPrimvarReader_float2>();
						tinyusdz::value::token token_uv;
						uvset->varname.get_value().value().get_scalar(&token_uv);
						material_mid.uvset = token_uv.str();
					}
				}
				else
				{
					opacity.get_value().get_scalar(&material_mid.opacity);					
				}

			}

			int idx = (int)material_lst.size();
			material_lst.push_back(material_mid);
			material_map[path] = idx;

		}

		if (prim.prim->data().type_id() != tinyusdz::value::TYPE_ID_MATERIAL
			&& prim.prim->data().type_id() != tinyusdz::value::TYPE_ID_GEOM_MESH)
		{			
			size_t num_children = prim.prim->children().size();
			for (size_t i = 0; i < num_children; i++)
			{
				queue_prim.push({ &prim.prim->children()[i], -1, path });
			}
		}
	}

	if (specular_used)
	{
		m_out.extensionsUsed.push_back("KHR_materials_pbrSpecularGlossiness");
	}

	std::unordered_map<std::string, int> joint_map;
	std::unordered_map<int, std::string> node_skin_map;
	std::unordered_map<std::string, int> skin_map;

	/*struct MorphIdx
	{
		int node_idx;
		int morph_idx;
	};

	std::unordered_map<std::string, MorphIdx> morph_map;*/

	queue_prim.push({ root_prim, -1, "" });
	while (!queue_prim.empty())
	{
		Prim prim = queue_prim.front();
		queue_prim.pop();
		std::string path = prim.base_path + "/" + prim.prim->element_path().full_path_name();

		if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_GEOM_XFORM)
		{
			auto* node_in = prim.prim->data().as<tinyusdz::Xform>();			

			{			
				auto iter = node_in->props.find("material:binding");
				if (iter != node_in->props.end())
				{
					prim.idx_material = material_map[iter->second.get_relationship().targetPath.full_path_name()];
				}
			}

			int node_id = (int)m_out.nodes.size();

			tinygltf::Node node_out;
			node_out.name = node_in->name;

			tinyusdz::value::matrix4d matrix;
			node_in->EvaluateXformOps(&matrix, nullptr, nullptr);

			glm::mat4 mat = *(glm::dmat4*)(&matrix);
			glm::vec3 scale;
			glm::quat rotation;
			glm::vec3 translation;

			glm::vec3 skew;
			glm::vec4 persp;
			glm::decompose(mat, scale, rotation, translation, skew, persp);

			node_out.translation = { translation.x, translation.y, translation.z };
			node_out.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
			node_out.scale = { scale.x, scale.y, scale.z };

			m_out.nodes.push_back(node_out);
			if (prim.id_node_base >= 0)
			{
				m_out.nodes[prim.id_node_base].children.push_back(node_id);
			}
			else
			{
				rotation = axis_rot * rotation;
				node_out.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
				scene_out.nodes.push_back(node_id);
			}
			prim.id_node_base = node_id;
		}
		else if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_SKEL_ROOT)
		{
			auto* node_in = prim.prim->data().as<tinyusdz::SkelRoot>();
			int node_id = (int)m_out.nodes.size();

			{
				auto iter = node_in->props.find("skel:skeleton");
				if (iter != node_in->props.end())
				{
					prim.skel_path = iter->second.get_relationship().targetPath.full_path_name();					
				}
			}

			tinygltf::Node node_out;
			node_out.name = node_in->name;
			
			for (size_t i = 0; i < node_in->xformOps.size(); i++)
			{
				auto& op = node_in->xformOps[i];				
				if (op.op_type == tinyusdz::XformOp::OpType::Transform)
				{
					auto* matrix = op.get_scalar().value().as<tinyusdz::value::matrix4d>();

					glm::mat4 mat = *(glm::dmat4*)(matrix);
					glm::vec3 scale;
					glm::quat rotation;
					glm::vec3 translation;

					glm::vec3 skew;
					glm::vec4 persp;
					glm::decompose(mat, scale, rotation, translation, skew, persp);

					rotation = axis_rot * rotation;
					
					node_out.translation = { translation.x, translation.y, translation.z };
					node_out.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
					node_out.scale = { scale.x, scale.y, scale.z };

					m_out.nodes.push_back(node_out);
					scene_out.nodes.push_back(node_id);

					prim.id_node_base = node_id;
					break;
				}
			}
		}
		else if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_GEOM_MESH)
		{
			auto* mesh_in = prim.prim->data().as<tinyusdz::GeomMesh>();

			bool leftHand = false;
			{
				auto iter = mesh_in->props.find("orientation");
				if (iter != mesh_in->props.end())
				{
					leftHand = iter->second.get_attribute().get_value<tinyusdz::Token>().value().str() == "leftHanded";
				}
			}						

			if (mesh_in->materialBinding.has_value())
			{
				prim.idx_material = material_map[mesh_in->materialBinding.value().binding.full_path_name()];
			}

			int node_id = (int)m_out.nodes.size();
			int mesh_id = (int)m_out.meshes.size();			

			tinygltf::Node node_out;
			node_out.name = mesh_in->name;
			node_out.mesh = mesh_id;
			m_out.nodes.push_back(node_out);			

			if (prim.id_node_base >= 0)
			{
				m_out.nodes[prim.id_node_base].children.push_back(node_id);
			}
			else
			{				
				node_out.rotation = { axis_rot.x, axis_rot.y, axis_rot.z, axis_rot.w };
				scene_out.nodes.push_back(node_id);
			}
			prim.id_node_base = node_id;

			tinygltf::Mesh mesh_out;
			mesh_out.name = node_out.name;
			mesh_out.primitives.resize(1);

			auto& prim_out = mesh_out.primitives[0];

			std::vector<tinyusdz::value::point3f> points_in;
			std::vector<tinyusdz::value::normal3f> norms_in;

			std::vector<std::vector<tinyusdz::value::vector3f>> offsets_in;
			std::vector<std::vector<tinyusdz::value::vector3f>> norm_offsets_in;
			std::vector<bool> target_sparse;
			std::vector<std::vector<bool>> non_zeros_in;

			std::vector<glm::u8vec4> conv_ji_in;
			std::vector<glm::vec4> conv_jw_in;

			std::vector<int> faceVertexIndices;
			std::vector<int> faceVertexCounts;
			
			std::vector<glm::ivec3> faces;

			bool uv_indp_indices = false;
			std::vector<tinyusdz::value::float2> uv_in;
			std::vector<int> uv_indices_in;

			int idx_material = prim.idx_material;
			if (idx_material == -1)
			{
				idx_material = (int)material_lst.size();
				Mid::Material material_mid;
				auto iter = mesh_in->props.find("primvars:displayColor");
				if (iter != mesh_in->props.end())
				{
					auto col = iter->second.get_attribute().get_value<std::vector<tinyusdz::value::float3>>().value()[0];
					material_mid.diffuse_color = { col[0], col[1], col[2] };
				}
				material_lst.push_back(material_mid);
				prim.idx_material = idx_material;
			}
			else
			{
				Mid::Material material_mid = material_lst[idx_material];
				auto iter = mesh_in->props.find("primvars:"+ material_mid.diffuse_varname);
				if (iter != mesh_in->props.end())
				{
					auto col = iter->second.get_attribute().get_value<std::vector<tinyusdz::value::float3>>().value()[0];
					material_mid.diffuse_color = { col[0], col[1], col[2] };
					idx_material = (int)material_lst.size();
					material_lst.push_back(material_mid);
					prim.idx_material = idx_material;
				}
			}

			Mid::Material& material_mid = material_lst[idx_material];
			material_mid.double_sided = mesh_in->doubleSided.get_value();

			prim_out.material = idx_material;
			
			mesh_in->points.get_value().value().get_scalar(&points_in);

			tinyusdz::Extent extent;
			mesh_in->extent.get_value().value().get_scalar(&extent);

			if (mesh_in->normals.get_value().has_value())
			{
				mesh_in->normals.get_value().value().get_scalar(&norms_in);
			}

			mesh_in->faceVertexIndices.get_value().value().get_scalar(&faceVertexIndices);
			mesh_in->faceVertexCounts.get_value().value().get_scalar(&faceVertexCounts);

			{			
				std::string var_name_uvset = std::string("primvars:") + material_mid.uvset;
				auto iter = mesh_in->props.find(var_name_uvset);
				if (iter != mesh_in->props.end())
				{
					uv_in = iter->second.get_attribute().get_value<std::vector<tinyusdz::value::float2>>().value();
					auto interpo = iter->second.get_attribute().metas().interpolation.value();
					uv_indp_indices = interpo == tinyusdz::Interpolation::FaceVarying;
					std::string var_name_uv_indices = std::string("primvars:") + material_mid.uvset + ":indices";
					auto iter2 = mesh_in->props.find(var_name_uv_indices);
					if (iter2 != mesh_in->props.end())
					{
						uv_indices_in = iter2->second.get_attribute().get_value<std::vector<int>>().value();
					}
				}
			}

			{
				auto iter_ji = mesh_in->props.find("primvars:skel:jointIndices");
				auto iter_jw = mesh_in->props.find("primvars:skel:jointWeights");
				if (iter_ji != mesh_in->props.end() && iter_jw != mesh_in->props.end())
				{
					if (mesh_in->skeleton.has_value())
					{
						prim.skel_path = mesh_in->skeleton.value().full_path_name();
					}
					node_skin_map[node_id] = prim.skel_path;

					unsigned elem_size = iter_ji->second.get_attribute().metas().elementSize.value();					
					bool constant_joints = iter_ji->second.get_attribute().metas().interpolation.value() == tinyusdz::Interpolation::Constant;

					auto ji = iter_ji->second.get_attribute().get_value<std::vector<int>>().value();
					auto jw = iter_jw->second.get_attribute().get_value<std::vector<float>>().value();
					size_t count = ji.size() / elem_size;

					if (constant_joints)
					{						
						count = points_in.size();
						conv_ji_in.resize(count, { 0,0,0,0 });
						conv_jw_in.resize(count, { 0.0f, 0.0f, 0.0f, 0.0f });

						for (size_t i = 0; i < count; i++)
						{
							unsigned elems = 4;
							if (elem_size < elems) elems = elem_size;
							for (unsigned j = 0; j < elems; j++)
							{								
								conv_ji_in[i][j] = (uint8_t)ji[j];
								conv_jw_in[i][j] = jw[j];
							}
						}
					}
					else
					{					

						conv_ji_in.resize(count, { 0,0,0,0 });
						conv_jw_in.resize(count, { 0.0f, 0.0f, 0.0f, 0.0f });

						for (size_t i = 0; i < count; i++)
						{
							unsigned elems = 4;
							if (elem_size < elems) elems = elem_size;
							for (unsigned j = 0; j < elems; j++)
							{
								size_t idx = elem_size * i + j;
								conv_ji_in[i][j] = (uint8_t)ji[idx];
								conv_jw_in[i][j] = jw[idx];
							}
						}
					}

				}

			}

			{
				auto iter = mesh_in->props.find("skel:blendShapeTargets");
				if (iter != mesh_in->props.end())
				{					
					auto paths = iter->second.get_relationship().targetPathVector;					
					auto iter2 = mesh_in->props.find("skel:blendShapes");
					auto names = iter2->second.get_attribute().get_value<std::vector<tinyusdz::Token>>().value();

					size_t num_morphs = paths.size();
					offsets_in.resize(num_morphs);
					norm_offsets_in.resize(num_morphs);
					target_sparse.resize(num_morphs, false);
					non_zeros_in.resize(num_morphs);
					for (size_t i = 0; i < num_morphs; i++)
					{						
						std::string name = names[i].str();
						 // morph_map[name] = { node_id, (int)i };

						const tinyusdz::Prim* primbs = stage.GetPrimAtPath(paths[i]).value();
						auto* bs = primbs->data().as<tinyusdz::BlendShape>();

						std::vector<tinyusdz::value::vector3f> offsets = bs->offsets.get_value().value();
						offsets_in[i].resize(points_in.size());

						std::vector<tinyusdz::value::vector3f> normOffsets;												
						if (norms_in.size() > 0)
						{
							normOffsets = bs->normalOffsets.get_value().value();
							norm_offsets_in[i].resize(norms_in.size());
						}

						non_zeros_in[i].resize(points_in.size(), false);
						
						if (bs->pointIndices.get_value().has_value())
						{
							target_sparse[i] = true;
							auto pointIndices = bs->pointIndices.get_value().value();
							size_t num_points = pointIndices.size();
							for (size_t j = 0; j < num_points; j++)
							{
								int idx = pointIndices[j];
								offsets_in[i][idx] = offsets[j];
								if (normOffsets.size() > 0)
								{
									norm_offsets_in[i][idx] = normOffsets[j];
								}
								non_zeros_in[i][idx] = true;
							}
						}
						else
						{
							target_sparse[i] = false;
							for (size_t j = 0; j < points_in.size(); j++)
							{
								offsets_in[i][j] = offsets[j];
								if (normOffsets.size() > 0)
								{
									norm_offsets_in[i][j] = normOffsets[j];
								}
								non_zeros_in[i][j] = true;
							}
						}
					}

				}
			}

			if (uv_indp_indices)
			{
				struct PointIn
				{
					int ind_pnt;
					tinyusdz::value::float2 uv;
				};

				std::vector<tinyusdz::value::point3f> points_out;
				std::vector<tinyusdz::value::normal3f> norms_out;

				std::vector<std::vector<tinyusdz::value::vector3f>> offsets_out;
				std::vector<std::vector<tinyusdz::value::vector3f>> norm_offsets_out;
				std::vector<std::vector<bool>> non_zeros_out;

				std::vector<glm::u8vec4> conv_ji_out;
				std::vector<glm::vec4> conv_jw_out;
				std::vector<tinyusdz::value::float2> uv_out;
				std::unordered_map<uint64_t, int> points_map;
				std::vector<int> faceVertexIndices_out(faceVertexIndices.size());

				size_t num_targets = offsets_in.size();
				if (num_targets > 0)
				{
					offsets_out.resize(num_targets);
					norm_offsets_out.resize(num_targets);
					non_zeros_out.resize(num_targets);
				}
				

				for (size_t i = 0; i < faceVertexIndices.size(); i++)
				{
					PointIn pnt;
					pnt.ind_pnt = faceVertexIndices[i];
					if (uv_indices_in.size() > 0)
					{			
						int idx_uv = uv_indices_in[i];
						pnt.uv = uv_in[idx_uv];
					}
					else
					{			
						pnt.uv = uv_in[i];
					}
					uint64_t hash = crc64(0, (unsigned char*)&pnt, sizeof(pnt));

					auto iter = points_map.find(hash);
					if (iter != points_map.end())
					{						
						faceVertexIndices_out[i] = iter->second;
					}
					else
					{
						int idx_out = (int)points_out.size();
						points_out.push_back(points_in[pnt.ind_pnt]);
						if (norms_in.size() > 0)
						{
							norms_out.push_back(norms_in[pnt.ind_pnt]);
						}

						if (num_targets > 0)
						{							
							for (size_t j = 0; j < num_targets; j++)
							{								
								offsets_out[j].push_back(offsets_in[j][pnt.ind_pnt]);							
								if (norm_offsets_in[j].size() > 0)
								{
									norm_offsets_out[j].push_back(norm_offsets_in[j][pnt.ind_pnt]);
								}								
								non_zeros_out[j].push_back(non_zeros_in[j][pnt.ind_pnt]);								
							}
							
						}

						if (conv_ji_in.size() > 0)
						{
							conv_ji_out.push_back(conv_ji_in[pnt.ind_pnt]);
							conv_jw_out.push_back(conv_jw_in[pnt.ind_pnt]);
						}
						uv_out.push_back(pnt.uv);
						faceVertexIndices_out[i] = idx_out;						
					}
				}								

				size_t idx_ind = 0;
				for (size_t i = 0; i < faceVertexCounts.size(); i++)
				{
					int count = faceVertexCounts[i];
					if (count == 3)
					{
						glm::ivec3 face;
						if (leftHand)
						{
							face.z = faceVertexIndices_out[idx_ind]; idx_ind++;
							face.y = faceVertexIndices_out[idx_ind]; idx_ind++;
							face.x = faceVertexIndices_out[idx_ind]; idx_ind++;
						}
						else
						{
							face.x = faceVertexIndices_out[idx_ind]; idx_ind++;
							face.y = faceVertexIndices_out[idx_ind]; idx_ind++;
							face.z = faceVertexIndices_out[idx_ind]; idx_ind++;
						}
						faces.push_back(face);
					}
					else if (count == 4)
					{
						glm::ivec3 face1;
						if (leftHand)
						{
							face1.z = faceVertexIndices_out[idx_ind]; idx_ind++;
							face1.y = faceVertexIndices_out[idx_ind]; idx_ind++;
							face1.x = faceVertexIndices_out[idx_ind]; idx_ind++;
						}
						else
						{
							face1.x = faceVertexIndices_out[idx_ind]; idx_ind++;
							face1.y = faceVertexIndices_out[idx_ind]; idx_ind++;
							face1.z = faceVertexIndices_out[idx_ind]; idx_ind++;
						}
						faces.push_back(face1);

						glm::ivec3 face2;
						face2.x = face1.z;
						face2.y = faceVertexIndices_out[idx_ind]; idx_ind++;
						face2.z = face1.x;
						faces.push_back(face2);
					}
				}

				offset = buf_out.data.size();
				length = points_out.size() * sizeof(glm::vec3);
				buf_out.data.resize(offset + length);
				memcpy(buf_out.data.data() + offset, points_out.data(), length);

				view_id = m_out.bufferViews.size();
				{
					tinygltf::BufferView view;
					view.buffer = 0;
					view.byteOffset = offset;
					view.byteLength = length;
					view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
					m_out.bufferViews.push_back(view);
				}

				acc_id = m_out.accessors.size();
				{
					tinygltf::Accessor acc;
					acc.bufferView = view_id;
					acc.byteOffset = 0;
					acc.type = TINYGLTF_TYPE_VEC3;
					acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
					acc.count = points_out.size();
					acc.minValues = { extent.lower[0], extent.lower[1], extent.lower[2] };
					acc.maxValues = { extent.upper[0], extent.upper[1], extent.upper[2] };
					m_out.accessors.push_back(acc);
				}

				prim_out.attributes["POSITION"] = acc_id;

				if (norms_out.size() > 0)
				{
					offset = buf_out.data.size();
					length = norms_out.size() * sizeof(glm::vec3);
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, norms_out.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.type = TINYGLTF_TYPE_VEC3;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = norms_out.size();
						m_out.accessors.push_back(acc);
					}

					prim_out.attributes["NORMAL"] = acc_id;
				}

				offset = buf_out.data.size();
				length = faces.size() * sizeof(glm::ivec3);
				buf_out.data.resize(offset + length);
				memcpy(buf_out.data.data() + offset, faces.data(), length);

				view_id = m_out.bufferViews.size();
				{
					tinygltf::BufferView view;
					view.buffer = 0;
					view.byteOffset = offset;
					view.byteLength = length;
					view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
					m_out.bufferViews.push_back(view);
				}

				acc_id = m_out.accessors.size();
				{
					tinygltf::Accessor acc;
					acc.bufferView = view_id;
					acc.byteOffset = 0;
					acc.type = TINYGLTF_TYPE_SCALAR;
					acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
					acc.count = faces.size() * 3;
					m_out.accessors.push_back(acc);
				}

				prim_out.indices = acc_id;

				if (num_targets > 0)
				{
					prim_out.targets.resize(num_targets);
					for (int lChannelIndex = 0; lChannelIndex < num_targets; ++lChannelIndex)
					{
						bool is_sparse = target_sparse[lChannelIndex];
						auto& offsets = offsets_out[lChannelIndex];
						auto& norm_offsets = norm_offsets_out[lChannelIndex];
						auto& non_zeros = non_zeros_out[lChannelIndex];

						size_t num_pos = points_out.size();

						{
							std::vector<int> indices;
							std::vector<glm::vec3> delta_pos;
							glm::vec3 min_pos = { 0.0f, 0.0f, 0.0f };
							glm::vec3 max_pos = { 0.0f, 0.0f, 0.0f };

							for (int k = 0; k < num_pos; k++)
							{
								if (non_zeros[k])
								{
									auto pos_offset = offsets[k];
									if (pos_offset.x < min_pos.x) min_pos.x = pos_offset.x;
									if (pos_offset.x > max_pos.x) max_pos.x = pos_offset.x;
									if (pos_offset.y < min_pos.y) min_pos.y = pos_offset.y;
									if (pos_offset.y > max_pos.y) max_pos.y = pos_offset.y;
									if (pos_offset.z < min_pos.z) min_pos.z = pos_offset.z;
									if (pos_offset.z > max_pos.z) max_pos.z = pos_offset.z;

									indices.push_back(k);
									delta_pos.push_back({ pos_offset.x, pos_offset.y, pos_offset.z });
								}
							}

							if (indices.size() < 1)
							{
								indices.push_back(0);
								delta_pos.push_back(glm::vec3(0.0f));
							}

							int num_verts = (int)indices.size();
							acc_id = m_out.accessors.size();
							if (is_sparse)
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;
								acc.sparse.isSparse = true;
								acc.sparse.count = num_verts;

								acc.maxValues = { max_pos.x, max_pos.y, max_pos.z };
								acc.minValues = { min_pos.x, min_pos.y, min_pos.z };

								offset = buf_out.data.size();
								length = sizeof(int) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, indices.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.indices.bufferView = view_id;
								acc.sparse.indices.byteOffset = 0;
								acc.sparse.indices.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_pos.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.values.bufferView = view_id;
								acc.sparse.values.byteOffset = 0;

								m_out.accessors.push_back(acc);
							}
							else
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;

								acc.maxValues = { max_pos.x, max_pos.y, max_pos.z };
								acc.minValues = { min_pos.x, min_pos.y, min_pos.z };

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_pos;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_pos.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.bufferView = view_id;
								acc.byteOffset = 0;
								m_out.accessors.push_back(acc);

							}

							prim_out.targets[lChannelIndex]["POSITION"] = acc_id;
						}

						if (norm_offsets.size() > 0)
						{
							std::vector<int> indices;
							std::vector<glm::vec3> delta_norm;

							for (int k = 0; k < num_pos; k++)
							{
								if (non_zeros[k])
								{
									auto norm_offset = norm_offsets[k];
									indices.push_back(k);
									delta_norm.push_back({ norm_offset.x, norm_offset.y, norm_offset.z });
								}
							}

							if (indices.size() < 1)
							{
								indices.push_back(0);
								delta_norm.push_back(glm::vec3(0.0f));
							}

							int num_verts = (int)indices.size();
							acc_id = m_out.accessors.size();
							if (is_sparse)
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;
								acc.sparse.isSparse = true;
								acc.sparse.count = num_verts;

								offset = buf_out.data.size();
								length = sizeof(int) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, indices.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.indices.bufferView = view_id;
								acc.sparse.indices.byteOffset = 0;
								acc.sparse.indices.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_norm.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.values.bufferView = view_id;
								acc.sparse.values.byteOffset = 0;

								m_out.accessors.push_back(acc);
							}
							else
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_pos;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_norm.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.bufferView = view_id;
								acc.byteOffset = 0;

								m_out.accessors.push_back(acc);
							}

							prim_out.targets[lChannelIndex]["NORMAL"] = acc_id;
						}


					}
				}

				size_t uv_count = uv_out.size();
				offset = buf_out.data.size();
				length = uv_count * sizeof(glm::vec2);
				buf_out.data.resize(offset + length);
				float* p_uv = (float*)(buf_out.data.data() + offset);
				for (size_t i = 0; i < uv_count; i++)
				{
					p_uv[i * 2] = uv_out[i][0];
					p_uv[i * 2 + 1] = 1.0f - uv_out[i][1];
				}				

				view_id = m_out.bufferViews.size();
				{
					tinygltf::BufferView view;
					view.buffer = 0;
					view.byteOffset = offset;
					view.byteLength = length;
					view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
					m_out.bufferViews.push_back(view);
				}

				acc_id = m_out.accessors.size();
				{
					tinygltf::Accessor acc;
					acc.bufferView = view_id;
					acc.byteOffset = 0;
					acc.type = TINYGLTF_TYPE_VEC2;
					acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
					acc.count = uv_count;
					m_out.accessors.push_back(acc);
				}

				prim_out.attributes["TEXCOORD_0"] = acc_id;

				if (conv_ji_out.size() > 0)
				{
					size_t count = conv_ji_out.size();

					offset = buf_out.data.size();
					length = sizeof(glm::u8vec4) * count;
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, conv_ji_out.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
						acc.count = count;
						acc.type = TINYGLTF_TYPE_VEC4;
						m_out.accessors.push_back(acc);
					}

					prim_out.attributes["JOINTS_0"] = acc_id;

					offset = buf_out.data.size();
					length = sizeof(glm::vec4) * count;
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, conv_jw_out.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = count;
						acc.type = TINYGLTF_TYPE_VEC4;
						m_out.accessors.push_back(acc);
					}

					prim_out.attributes["WEIGHTS_0"] = acc_id;

				}
			}
			else
			{
				size_t idx_ind = 0;
				for (size_t i = 0; i < faceVertexCounts.size(); i++)
				{
					int count = faceVertexCounts[i];
					if (count == 3)
					{
						glm::ivec3 face;
						if (leftHand)
						{
							face.z = faceVertexIndices[idx_ind]; idx_ind++;
							face.y = faceVertexIndices[idx_ind]; idx_ind++;
							face.x = faceVertexIndices[idx_ind]; idx_ind++;
						}
						else
						{
							face.x = faceVertexIndices[idx_ind]; idx_ind++;
							face.y = faceVertexIndices[idx_ind]; idx_ind++;
							face.z = faceVertexIndices[idx_ind]; idx_ind++;
						}
						faces.push_back(face);
					}
					else if (count == 4)
					{
						glm::ivec3 face1;
						if (leftHand)
						{
							face1.z = faceVertexIndices[idx_ind]; idx_ind++;
							face1.y = faceVertexIndices[idx_ind]; idx_ind++;
							face1.x = faceVertexIndices[idx_ind]; idx_ind++;
						}
						else
						{
							face1.x = faceVertexIndices[idx_ind]; idx_ind++;
							face1.y = faceVertexIndices[idx_ind]; idx_ind++;
							face1.z = faceVertexIndices[idx_ind]; idx_ind++;
						}
						faces.push_back(face1);

						glm::ivec3 face2;
						face2.x = face1.z;
						face2.y = faceVertexIndices[idx_ind]; idx_ind++;
						face2.z = face1.x;
						faces.push_back(face2);
					}
				}

				offset = buf_out.data.size();
				length = points_in.size() * sizeof(glm::vec3);
				buf_out.data.resize(offset + length);
				memcpy(buf_out.data.data() + offset, points_in.data(), length);

				view_id = m_out.bufferViews.size();
				{
					tinygltf::BufferView view;
					view.buffer = 0;
					view.byteOffset = offset;
					view.byteLength = length;
					view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
					m_out.bufferViews.push_back(view);
				}

				acc_id = m_out.accessors.size();
				{
					tinygltf::Accessor acc;
					acc.bufferView = view_id;
					acc.byteOffset = 0;
					acc.type = TINYGLTF_TYPE_VEC3;
					acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
					acc.count = points_in.size();
					acc.minValues = { extent.lower[0], extent.lower[1], extent.lower[2] };
					acc.maxValues = { extent.upper[0], extent.upper[1], extent.upper[2] };
					m_out.accessors.push_back(acc);
				}

				prim_out.attributes["POSITION"] = acc_id;

				if (norms_in.size() > 0)
				{
					offset = buf_out.data.size();
					length = norms_in.size() * sizeof(glm::vec3);
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, norms_in.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.type = TINYGLTF_TYPE_VEC3;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = norms_in.size();
						m_out.accessors.push_back(acc);
					}

					prim_out.attributes["NORMAL"] = acc_id;
				}

				offset = buf_out.data.size();
				length = faces.size() * sizeof(glm::ivec3);
				buf_out.data.resize(offset + length);
				memcpy(buf_out.data.data() + offset, faces.data(), length);

				view_id = m_out.bufferViews.size();
				{
					tinygltf::BufferView view;
					view.buffer = 0;
					view.byteOffset = offset;
					view.byteLength = length;
					view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
					m_out.bufferViews.push_back(view);
				}

				acc_id = m_out.accessors.size();
				{
					tinygltf::Accessor acc;
					acc.bufferView = view_id;
					acc.byteOffset = 0;
					acc.type = TINYGLTF_TYPE_SCALAR;
					acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
					acc.count = faces.size() * 3;
					m_out.accessors.push_back(acc);
				}

				prim_out.indices = acc_id;

				int num_targets = offsets_in.size();
				if (num_targets > 0)
				{
					prim_out.targets.resize(num_targets);
					for (int lChannelIndex = 0; lChannelIndex < num_targets; ++lChannelIndex)
					{
						bool is_sparse = target_sparse[lChannelIndex];
						auto& offsets = offsets_in[lChannelIndex];
						auto& norm_offsets = norm_offsets_in[lChannelIndex];
						auto& non_zeros = non_zeros_in[lChannelIndex];

						size_t num_pos = points_in.size();

						{
							std::vector<int> indices;
							std::vector<glm::vec3> delta_pos;
							glm::vec3 min_pos = { 0.0f, 0.0f, 0.0f };
							glm::vec3 max_pos = { 0.0f, 0.0f, 0.0f };

							for (int k = 0; k < num_pos; k++)
							{
								if (non_zeros[k])
								{
									auto pos_offset = offsets[k];
									if (pos_offset.x < min_pos.x) min_pos.x = pos_offset.x;
									if (pos_offset.x > max_pos.x) max_pos.x = pos_offset.x;
									if (pos_offset.y < min_pos.y) min_pos.y = pos_offset.y;
									if (pos_offset.y > max_pos.y) max_pos.y = pos_offset.y;
									if (pos_offset.z < min_pos.z) min_pos.z = pos_offset.z;
									if (pos_offset.z > max_pos.z) max_pos.z = pos_offset.z;

									indices.push_back(k);
									delta_pos.push_back({ pos_offset.x, pos_offset.y, pos_offset.z });
								}
							}

							if (indices.size() < 1)
							{
								indices.push_back(0);
								delta_pos.push_back(glm::vec3(0.0f));
							}

							int num_verts = (int)indices.size();
							acc_id = m_out.accessors.size();
							if (is_sparse)
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;
								acc.sparse.isSparse = true;
								acc.sparse.count = num_verts;

								acc.maxValues = { max_pos.x, max_pos.y, max_pos.z };
								acc.minValues = { min_pos.x, min_pos.y, min_pos.z };

								offset = buf_out.data.size();
								length = sizeof(int) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, indices.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.indices.bufferView = view_id;
								acc.sparse.indices.byteOffset = 0;
								acc.sparse.indices.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_pos.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.values.bufferView = view_id;
								acc.sparse.values.byteOffset = 0;

								m_out.accessors.push_back(acc);
							}
							else
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;

								acc.maxValues = { max_pos.x, max_pos.y, max_pos.z };
								acc.minValues = { min_pos.x, min_pos.y, min_pos.z };

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_pos;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_pos.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.bufferView = view_id;
								acc.byteOffset = 0;
								m_out.accessors.push_back(acc);

							}

							prim_out.targets[lChannelIndex]["POSITION"] = acc_id;
						}

						if (norm_offsets.size() > 0)
						{
							std::vector<int> indices;
							std::vector<glm::vec3> delta_norm;

							for (int k = 0; k < num_pos; k++)
							{
								if (non_zeros[k])
								{
									auto norm_offset = norm_offsets[k];
									indices.push_back(k);
									delta_norm.push_back({ norm_offset.x, norm_offset.y, norm_offset.z });
								}								
							}

							if (indices.size() < 1)
							{
								indices.push_back(0);
								delta_norm.push_back(glm::vec3(0.0f));
							}

							int num_verts = (int)indices.size();
							acc_id = m_out.accessors.size();
							if (is_sparse)
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;
								acc.sparse.isSparse = true;
								acc.sparse.count = num_verts;

								offset = buf_out.data.size();
								length = sizeof(int) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, indices.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.indices.bufferView = view_id;
								acc.sparse.indices.byteOffset = 0;
								acc.sparse.indices.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_verts;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_norm.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.sparse.values.bufferView = view_id;
								acc.sparse.values.byteOffset = 0;

								m_out.accessors.push_back(acc);
							}
							else
							{
								tinygltf::Accessor acc;
								acc.byteOffset = 0;
								acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
								acc.count = (size_t)(num_pos);
								acc.type = TINYGLTF_TYPE_VEC3;

								offset = buf_out.data.size();
								length = sizeof(glm::vec3) * num_pos;
								buf_out.data.resize(offset + length);
								memcpy(buf_out.data.data() + offset, delta_norm.data(), length);

								view_id = m_out.bufferViews.size();
								{
									tinygltf::BufferView view;
									view.buffer = 0;
									view.byteOffset = offset;
									view.byteLength = length;
									m_out.bufferViews.push_back(view);
								}

								acc.bufferView = view_id;
								acc.byteOffset = 0;

								m_out.accessors.push_back(acc);
							}

							prim_out.targets[lChannelIndex]["NORMAL"] = acc_id;
						}


					}

				}

				if (uv_in.size() > 0)
				{
					size_t uv_count;
					if (uv_indices_in.size() > 0)
					{
						uv_count = uv_indices_in.size();
						offset = buf_out.data.size();
						length = uv_count * sizeof(glm::vec2);
						buf_out.data.resize(offset + length);
						float* p_uv = (float*)(buf_out.data.data() + offset);
						for (size_t i = 0; i < uv_count; i++)
						{
							int idx = uv_indices_in[i];
							p_uv[i * 2] = uv_in[idx][0];
							p_uv[i * 2 + 1] = 1.0f - uv_in[idx][1];
						}

					}
					else
					{
						uv_count = uv_in.size();
						offset = buf_out.data.size();
						length = uv_count * sizeof(glm::vec2);
						buf_out.data.resize(offset + length);
						float* p_uv = (float*)(buf_out.data.data() + offset);
						for (size_t i = 0; i < uv_count; i++)
						{
							p_uv[i * 2] = uv_in[i][0];
							p_uv[i * 2 + 1] = 1.0f - uv_in[i][1];
						}						
					}

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.type = TINYGLTF_TYPE_VEC2;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = uv_count;
						m_out.accessors.push_back(acc);
					}

					prim_out.attributes["TEXCOORD_0"] = acc_id;

				}

				if (conv_ji_in.size()>0)
				{
					size_t count = conv_ji_in.size();

					offset = buf_out.data.size();
					length = sizeof(glm::u8vec4) * count;
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, conv_ji_in.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
						acc.count = count;
						acc.type = TINYGLTF_TYPE_VEC4;
						m_out.accessors.push_back(acc);
					}

					prim_out.attributes["JOINTS_0"] = acc_id;

					offset = buf_out.data.size();
					length = sizeof(glm::vec4) * count;
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, conv_jw_in.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = count;
						acc.type = TINYGLTF_TYPE_VEC4;
						m_out.accessors.push_back(acc);
					}

					prim_out.attributes["WEIGHTS_0"] = acc_id;

				}
			}

			
			prim_out.mode = TINYGLTF_MODE_TRIANGLES;
			m_out.meshes.push_back(mesh_out);

		}
		else if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_SKELETON)
		{
			int skin_idx = (int)m_out.skins.size();
			m_out.skins.resize(skin_idx + 1);
			tinygltf::Skin& skin_out = m_out.skins[skin_idx];
			skin_out.skeleton = prim.id_node_base;
			skin_map[path] = skin_idx;

			auto* skel_in = prim.prim->data().as<tinyusdz::Skeleton>();
			auto bindTrans = skel_in->bindTransforms.get_value().value();
			auto joints = skel_in->joints.get_value().value();
			auto restTrans = skel_in->restTransforms.get_value().value();

			std::vector<glm::mat4> inv_binding_matrices(bindTrans.size());

			for (size_t i = 0; i < joints.size(); i++)
			{
				int node_id = (int)m_out.nodes.size();				

				skin_out.joints.push_back(node_id);

				std::string path = joints[i].str();
				joint_map[path] = node_id;

				auto bind = bindTrans[i];
				glm::mat4 bindMat;

				const double* pDBindMat = (double*)(&bind);
				float* pFBindMat = (float*)(&bindMat);
				for (int j = 0; j < 16; j++)
				{
					pFBindMat[j] = (float)pDBindMat[j];
				}
				inv_binding_matrices[i] = glm::inverse(bindMat);

				auto rest = restTrans[i];

				tinygltf::Node node_out;

				glm::mat4 mat = *(glm::dmat4*)(&rest);
				glm::vec3 scale;
				glm::quat rotation;
				glm::vec3 translation;

				glm::vec3 skew;
				glm::vec4 persp;
				glm::decompose(mat, scale, rotation, translation, skew, persp);

				node_out.translation = { translation.x, translation.y, translation.z };
				node_out.rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
				node_out.scale = { scale.x, scale.y, scale.z };

				size_t pos = path.rfind('/');
				if (pos == std::string::npos)
				{
					node_out.name = path;					
					if (prim.id_node_base >= 0)
					{
						m_out.nodes[prim.id_node_base].children.push_back(node_id);						
					}
					else
					{
						scene_out.nodes.push_back(node_id);
					}
				}
				else
				{
					node_out.name = path.substr(pos + 1);
					int id_parent = joint_map[path.substr(0, pos)];
					m_out.nodes[id_parent].children.push_back(node_id);
				}
				m_out.nodes.push_back(node_out);

			}

			offset = buf_out.data.size();
			length = sizeof(glm::mat4) * inv_binding_matrices.size();
			buf_out.data.resize(offset + length);
			memcpy(buf_out.data.data() + offset, inv_binding_matrices.data(), length);

			view_id = m_out.bufferViews.size();
			{
				tinygltf::BufferView view;
				view.buffer = 0;
				view.byteOffset = offset;
				view.byteLength = length;
				m_out.bufferViews.push_back(view);
			}

			acc_id = m_out.accessors.size();
			{
				tinygltf::Accessor acc;
				acc.bufferView = view_id;
				acc.byteOffset = 0;
				acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
				acc.count = inv_binding_matrices.size();
				acc.type = TINYGLTF_TYPE_MAT4;
				m_out.accessors.push_back(acc);
			}

			skin_out.inverseBindMatrices = acc_id;
		}

		if (prim.prim->data().type_id() != tinyusdz::value::TYPE_ID_MATERIAL
			&& prim.prim->data().type_id() != tinyusdz::value::TYPE_ID_GEOM_MESH)
		{			
			size_t num_children = prim.prim->children().size();
			for (size_t i = 0; i < num_children; i++)
			{
				queue_prim.push({ &prim.prim->children()[i], prim.id_node_base, path, prim.idx_material, prim.skel_path });
			}
		}
	}

	std::vector<Mid::Image> tex_lst;	

	for (size_t i = 0; i < material_lst.size(); i++)
	{
		auto& material = material_lst[i];
		if (material.diffuse_tex != "" || material.opacity_tex !="")
		{
			Mid::Image img_diffuse, img_opacity;
			if (material.diffuse_tex != "")
			{
				img_diffuse.Load((path_model+ "/" + material.diffuse_tex).c_str());
			}
			if (material.opacity_tex != "")
			{
				img_opacity.Load((path_model + "/" + material.opacity_tex).c_str());
			}

			int idx = (int)tex_lst.size();
			tex_lst.resize(idx + 1);

			Mid::Image& img = tex_lst[idx];
			img.CreateRGBA(img_diffuse, img_opacity);
			material.idx_diffuse_alpha = idx;
		}
		if (material.emissive_tex != "")
		{
			int idx = (int)tex_lst.size();
			tex_lst.resize(idx + 1);

			Mid::Image& img = tex_lst[idx];
			img.Load((path_model + "/" + material.emissive_tex).c_str());
			
			material.idx_emissive = idx;
		}

		if (material.useSpecularWorkflow)
		{
			if (material.specular_tex != "" || material.roughness_tex != "")
			{
				Mid::Image img_specular, img_roughness;
				if (material.specular_tex != "")
				{
					img_specular.Load((path_model + "/" + material.specular_tex).c_str());
				}
				if (material.roughness_tex != "")
				{
					img_roughness.Load((path_model + "/" + material.roughness_tex).c_str());
				}

				int idx = (int)tex_lst.size();
				tex_lst.resize(idx + 1);

				Mid::Image& img = tex_lst[idx];
				img.CreateSG(img_specular, img_roughness, material.roughness);
				material.idx_specular_glossiness = idx;

			}
		}
		else
		{
			if (material.metallic_tex != "" || material.roughness_tex != "")
			{
				Mid::Image img_metallic, img_roughness;
				if (material.metallic_tex != "")
				{
					img_metallic.Load((path_model + "/" + material.metallic_tex).c_str());
				}
				if (material.roughness_tex != "")
				{
					img_roughness.Load((path_model + "/" + material.roughness_tex).c_str());
				}

				int idx = (int)tex_lst.size();
				tex_lst.resize(idx + 1);

				Mid::Image& img = tex_lst[idx];
				img.CreateMR(img_metallic, img_roughness);
				material.idx_metallic_roughness = idx;

			}
		}
	}

	m_out.samplers.resize(1);
	tinygltf::Sampler& sampler = m_out.samplers[0];
	sampler.minFilter = TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
	sampler.magFilter = TINYGLTF_TEXTURE_FILTER_LINEAR;

	m_out.images.resize(tex_lst.size());
	m_out.textures.resize(tex_lst.size());
	for (size_t i = 0; i < tex_lst.size(); i++)
	{
		Mid::Image& img_mid = tex_lst[i];
		tinygltf::Image& img_out = m_out.images[i];
		tinygltf::Texture& tex_out = m_out.textures[i];

		length = img_mid.code.size();
		offset = buf_out.data.size();
		buf_out.data.resize((offset + length + 3) / 4 * 4);
		memcpy(buf_out.data.data() + offset, img_mid.code.data(), length);

		img_out.width = img_mid.width;
		img_out.height = img_mid.height;
		img_out.component = 4;
		img_out.bits = 8;
		img_out.pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
		img_out.mimeType = img_mid.mimeType;

		view_id = m_out.bufferViews.size();
		{
			tinygltf::BufferView view;
			view.buffer = 0;
			view.byteOffset = offset;
			view.byteLength = length;
			m_out.bufferViews.push_back(view);
		}
		img_out.bufferView = view_id;

		tex_out.sampler = 0;
		tex_out.source = i;
	}

	m_out.materials.resize(material_lst.size());
	for (size_t i = 0; i < material_lst.size(); i++)
	{
		auto& material_mid = material_lst[i];
		tinygltf::Material& material_out = m_out.materials[i];
		material_out.name = material_mid.name;
		material_out.doubleSided = material_mid.double_sided;

		material_out.pbrMetallicRoughness.baseColorFactor = { material_mid.diffuse_color[0], material_mid.diffuse_color[1], material_mid.diffuse_color[2], material_mid.opacity };

		if (material_mid.idx_diffuse_alpha >= 0)
		{
			material_out.pbrMetallicRoughness.baseColorTexture.index = material_mid.idx_diffuse_alpha;
		}
		else
		{
			material_out.pbrMetallicRoughness.baseColorTexture.index = -1;
		}		

		material_out.emissiveFactor = { material_mid.emissive_color[0], material_mid.emissive_color[1], material_mid.emissive_color[2] };
		if (material_mid.idx_emissive >= 0)
		{			
			material_out.emissiveTexture.index = material_mid.idx_emissive;
		}
		else
		{			
			material_out.emissiveTexture.index = -1;
		}

		material_out.pbrMetallicRoughness.metallicFactor = material_mid.metallic;
		material_out.pbrMetallicRoughness.roughnessFactor = material_mid.roughness;
		if (material_mid.idx_metallic_roughness >= 0)
		{			
			material_out.pbrMetallicRoughness.metallicRoughnessTexture.index = material_mid.idx_metallic_roughness;			
		}
		else
		{
			material_out.pbrMetallicRoughness.metallicRoughnessTexture.index = -1;
		}

		if (material_mid.useSpecularWorkflow)
		{
			tinygltf::Value::Object sg;
			{
				std::vector<tinygltf::Value> color(4);
				color[0] = tinygltf::Value(material_mid.diffuse_color[0]);
				color[1] = tinygltf::Value(material_mid.diffuse_color[1]);
				color[2] = tinygltf::Value(material_mid.diffuse_color[2]);
				color[3] = tinygltf::Value(material_mid.opacity);
				sg["diffuseFactor"] = tinygltf::Value(color);

				if (material_mid.idx_diffuse_alpha >= 0)
				{
					tinygltf::Value::Object tex;
					tex["index"] = tinygltf::Value(material_mid.idx_diffuse_alpha);
					sg["diffuseTexture"] = tinygltf::Value(tex);
				}				
				
				
			}
			{
				std::vector<tinygltf::Value> color(3);
				color[0] = tinygltf::Value(material_mid.specular_color[0]);
				color[1] = tinygltf::Value(material_mid.specular_color[1]);
				color[2] = tinygltf::Value(material_mid.specular_color[2]);
				sg["specularFactor"] = tinygltf::Value(color);

				tinygltf::Value v;
				if (material_mid.idx_specular_glossiness >= 0)
				{					
					v = tinygltf::Value(1.0);
					tinygltf::Value::Object tex;
					tex["index"] = tinygltf::Value(material_mid.idx_specular_glossiness);
					sg["specularGlossinessTexture"] = tinygltf::Value(tex);
				}
				else
				{
					v = tinygltf::Value(1.0 - material_mid.roughness);					
				}				
				sg["glossinessFactor"] = v;
				
			}

			material_out.extensions["KHR_materials_pbrSpecularGlossiness"] = tinygltf::Value(sg);
		}
		
	}

	auto iter = node_skin_map.begin();
	while (iter != node_skin_map.end())
	{
		int node_idx = iter->first;
		std::string skin_path = iter->second;
		int skin_idx = skin_map[skin_path];
		m_out.nodes[node_idx].skin = skin_idx;
		iter++;
	}

	queue_prim.push({ root_prim, -1, "" });
	while (!queue_prim.empty())
	{
		Prim prim = queue_prim.front();
		queue_prim.pop();
		std::string path = prim.base_path + "/" + prim.prim->element_path().full_path_name();

		if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_SKELANIMATION)
		{
			auto* anim_in = prim.prim->data().as<tinyusdz::SkelAnimation>();
			int id_anim = (int)m_out.animations.size();
			m_out.animations.resize(id_anim + 1);

			tinygltf::Animation& anim_out = m_out.animations[id_anim];
			anim_out.name = anim_in->name;

			bool has_translations = anim_in->translations.get_value().has_value();
			bool has_rotations = anim_in->rotations.get_value().has_value();
			bool has_scales = anim_in->scales.get_value().has_value();

			auto joints = anim_in->joints.get_value().value();
			for (size_t i = 0; i < joints.size(); i++)
			{
				std::string joint_path = joints[i].str();
				auto iter = joint_map.find(joint_path);
				if (iter == joint_map.end()) continue;
				int id_node = joint_map[joint_path];

				if (has_translations)
				{
					auto translations = anim_in->translations.get_value().value().get_timesamples().get_samples();

					int id_channel = (int)anim_out.channels.size();
					anim_out.channels.resize(id_channel + 1);
					tinygltf::AnimationChannel& channel = anim_out.channels[id_channel];
					channel.target_node = id_node;
					channel.target_path = "translation";

					int id_sampler = (int)anim_out.samplers.size();
					channel.sampler = id_sampler;

					anim_out.samplers.resize(id_sampler + 1);
					tinygltf::AnimationSampler& sampler = anim_out.samplers[id_sampler];

					std::vector<float> times(translations.size());
					std::vector<glm::vec3> values(translations.size());

					for (size_t j = 0; j < translations.size(); j++)
					{
						times[j] = (float)(translations[j].t / time_codes_per_sec);
						auto tran_in = translations[j].value[i];
						values[j] = glm::vec3(tran_in[0], tran_in[1], tran_in[2]);
					}

					float t0 = times[0];
					float t1 = times[times.size() - 1];

					offset = buf_out.data.size();
					length = sizeof(float) * times.size();
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, times.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = times.size();
						acc.type = TINYGLTF_TYPE_SCALAR;
						acc.minValues = { t0 };
						acc.maxValues = { t1 };
						m_out.accessors.push_back(acc);
					}

					sampler.input = acc_id;

					offset = buf_out.data.size();
					length = sizeof(glm::vec3) * values.size();
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, values.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = values.size();
						acc.type = TINYGLTF_TYPE_VEC3;
						m_out.accessors.push_back(acc);
					}

					sampler.output = acc_id;

				}

				if (has_rotations)
				{
					auto rotations = anim_in->rotations.get_value().value().get_timesamples().get_samples();

					int id_channel = (int)anim_out.channels.size();
					anim_out.channels.resize(id_channel + 1);
					tinygltf::AnimationChannel& channel = anim_out.channels[id_channel];
					channel.target_node = id_node;
					channel.target_path = "rotation";

					int id_sampler = (int)anim_out.samplers.size();
					channel.sampler = id_sampler;

					anim_out.samplers.resize(id_sampler + 1);
					tinygltf::AnimationSampler& sampler = anim_out.samplers[id_sampler];

					std::vector<float> times(rotations.size());
					std::vector<glm::quat> values(rotations.size());

					for (size_t j = 0; j < rotations.size(); j++)
					{
						times[j] = (float)(rotations[j].t / time_codes_per_sec);
						auto rot_in = rotations[j].value[i];
						values[j] = glm::quat(rot_in.real, rot_in.imag[0], rot_in.imag[1], rot_in.imag[2]);
					}

					float t0 = times[0];
					float t1 = times[times.size() - 1];

					offset = buf_out.data.size();
					length = sizeof(float) * times.size();
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, times.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = times.size();
						acc.type = TINYGLTF_TYPE_SCALAR;
						acc.minValues = { t0 };
						acc.maxValues = { t1 };
						m_out.accessors.push_back(acc);
					}
					sampler.input = acc_id;

					offset = buf_out.data.size();
					length = sizeof(float) * 4 * values.size();
					buf_out.data.resize(offset + length);
					for (size_t k = 0; k < values.size(); k++)
					{
						float* p_out = (float*)(buf_out.data.data() + offset) + k * 4;
						glm::quat rot = values[k];
						p_out[0] = rot.x;
						p_out[1] = rot.y;
						p_out[2] = rot.z;
						p_out[3] = rot.w;
					}

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = values.size();
						acc.type = TINYGLTF_TYPE_VEC4;
						m_out.accessors.push_back(acc);
					}

					sampler.output = acc_id;

				}

#if 0
				if (has_scales)
				{
					auto scales = anim_in->scales.GetValue().value().ts.GetSamples();

					int id_channel = (int)anim_out.channels.size();
					anim_out.channels.resize(id_channel + 1);
					tinygltf::AnimationChannel& channel = anim_out.channels[id_channel];
					channel.target_node = id_node;
					channel.target_path = "scale";

					int id_sampler = (int)anim_out.samplers.size();
					channel.sampler = id_sampler;

					anim_out.samplers.resize(id_sampler + 1);
					tinygltf::AnimationSampler& sampler = anim_out.samplers[id_sampler];

					std::vector<float> times(scales.size());
					std::vector<glm::vec3> values(scales.size());

					for (size_t j = 0; j < scales.size(); j++)
					{
						times[j] = (float)(scales[j].t / time_codes_per_sec);
						auto scale_in = scales[j].value[i];
						values[j] = glm::vec3(half_to_float(scale_in[0]), half_to_float(scale_in[1]), half_to_float(scale_in[2]));
					}

					float t0 = times[0];
					float t1 = times[times.size() - 1];

					offset = buf_out.data.size();
					length = sizeof(float) * times.size();
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, times.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = times.size();
						acc.type = TINYGLTF_TYPE_SCALAR;
						acc.minValues = { t0 };
						acc.maxValues = { t1 };
						m_out.accessors.push_back(acc);
					}

					sampler.input = acc_id;

					offset = buf_out.data.size();
					length = sizeof(glm::vec3) * values.size();
					buf_out.data.resize(offset + length);
					memcpy(buf_out.data.data() + offset, values.data(), length);

					view_id = m_out.bufferViews.size();
					{
						tinygltf::BufferView view;
						view.buffer = 0;
						view.byteOffset = offset;
						view.byteLength = length;
						m_out.bufferViews.push_back(view);
					}

					acc_id = m_out.accessors.size();
					{
						tinygltf::Accessor acc;
						acc.bufferView = view_id;
						acc.byteOffset = 0;
						acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
						acc.count = values.size();
						acc.type = TINYGLTF_TYPE_VEC3;
						m_out.accessors.push_back(acc);
					}

					sampler.output = acc_id;
				}

#endif			
			}

			/*
			if (anim_in->blendShapes.get_value().has_value())
			{
				auto bs_names = anim_in->blendShapes.get_value().value();
				std::vector<MorphIdx> morphIdx(bs_names.size());
				for (size_t i = 0; i < bs_names.size(); i++)
				{
					morphIdx[i] = morph_map[bs_names[i].str()];
				}

				auto weights = anim_in->blendShapeWeights.get_value().value().get_timesamples().get_samples();

				int id_channel = (int)anim_out.channels.size();
				anim_out.channels.resize(id_channel + bs_names.size());
				tinygltf::AnimationChannel& channel = anim_out.channels[id_channel];

			}*/
		}

		if (prim.prim->data().type_id() != tinyusdz::value::TYPE_ID_MATERIAL
			&& prim.prim->data().type_id() != tinyusdz::value::TYPE_ID_GEOM_MESH)
		{
			int id_node_base = (int)(m_out.nodes.size() - 1);
			size_t num_children = prim.prim->children().size();
			for (size_t i = 0; i < num_children; i++)
			{
				queue_prim.push({ &prim.prim->children()[i],id_node_base, path });
			}
		}
	}

	tinygltf::TinyGLTF gltf;
	gltf.WriteGltfSceneToFile(&m_out, argv[2], true, true, false, true);

	return 0;
}
#endif
