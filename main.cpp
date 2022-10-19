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
#include <vector>
#include <queue>
#include <unordered_map>

#include "Image.h"

namespace Mid
{
	struct Material
	{
		std::string name;
		bool double_sided = false;

		glm::vec3 diffuse_color = glm::vec3(1.0f, 1.0f, 1.0f);
		std::string diffuse_tex = "";

		glm::vec3 emissive_color = glm::vec3(0.0f, 0.0f, 0.0f);
		std::string emissive_tex = "";

		float metallic = 0.0f;
		std::string metallic_tex = "";

		float roughness = 0.5f;
		std::string roughness_tex = "";

		std::string uvset = "";

		int idx_diffuse = -1;
		int idx_emissive = -1;
		int idx_metallic_roughness = -1;
	};
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		printf("usd2glb input.usdc output.glb\n");
		return 0;
	}

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

	//tinyusdz::usda::SaveAsUSDA("output.udsa", stage, &warn, &err);

	tinyusdz::Prim* root_prim = &stage.GetRootPrims()[0];

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

	std::unordered_map<std::string, int> skeleton_map;

	struct Prim
	{
		tinyusdz::Prim* prim;
		int id_node_base = -1;
		std::string base_path;
	};

	std::queue<Prim> queue_prim;
	queue_prim.push({ root_prim, -1, ""});

	while (!queue_prim.empty())
	{
		Prim prim = queue_prim.front();
		queue_prim.pop();
		std::string path = prim.base_path + "/" + prim.prim->local_path().full_path_name();		

		if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_GEOM_XFORM)
		{
			auto* node_in = prim.prim->data().as<tinyusdz::Xform>();
			int node_id = (int)m_out.nodes.size();

			tinygltf::Node node_out;
			node_out.name = node_in->name;

			tinyusdz::value::matrix4d matrix;
			node_in->EvaluateXformOps(&matrix, nullptr, nullptr);
			node_out.matrix.resize(16);
			memcpy(node_out.matrix.data(), &matrix, sizeof(double) * 16);
			m_out.nodes.push_back(node_out);
			if (prim.id_node_base >= 0)
			{
				m_out.nodes[prim.id_node_base].children.push_back(node_id);
			}
			else
			{
				scene_out.nodes.push_back(node_id);
			}
			
		}
		else if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_MATERIAL)
		{
			Mid::Material material_mid;

			auto* material_in = prim.prim->data().as<tinyusdz::Material>();
			material_mid.name = material_in->name;
			std::string outputs_surface = material_in->surface->target.value().GetPrimPart();			
			const tinyusdz::Prim* pshader0 = stage.GetPrimAtPath(tinyusdz::Path(outputs_surface, "")).value();			
			auto* shader0 = pshader0->data().as<tinyusdz::Shader>();
			auto* surface = shader0->value.as<tinyusdz::UsdPreviewSurface>();
			{
				auto diffuse = surface->diffuseColor;
				auto diffuse_connection = diffuse.GetConnection();
				if (diffuse_connection.has_value())
				{
					std::string path = diffuse_connection.value().GetPrimPart();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					auto file = tex->file.GetValue().value().value;
					material_mid.diffuse_tex = file.GetAssetPath();					

					auto uv_connection = tex->st.GetConnection();
					if (uv_connection.has_value())
					{
						std::string path_uv = uv_connection.value().GetPrimPart();
						const tinyusdz::Prim* pshader2 = stage.GetPrimAtPath(tinyusdz::Path(path_uv, "")).value();
						auto* shader2 = pshader2->data().as<tinyusdz::Shader>();
						auto* uvset = shader2->value.as<tinyusdz::UsdPrimvarReader_float2>();
						material_mid.uvset = uvset->varname.GetValue().value().value.str();					
					}

				}
				else
				{
					auto col = diffuse.GetValue().value;
					material_mid.diffuse_color = { col[0], col[1], col[2] };
				}
			}
			{
				auto emissive = surface->emissiveColor;
				auto emissive_connection = emissive.GetConnection();
				if (emissive_connection.has_value())
				{
					std::string path = emissive_connection.value().GetPrimPart();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					auto file = tex->file.GetValue().value().value;
					material_mid.emissive_tex = file.GetAssetPath();
				}
				else
				{
					auto col = emissive.GetValue().value;
					material_mid.emissive_color = { col[0], col[1], col[2] };
				}
			}
			{
				auto metallic = surface->metallic;
				auto metallic_connection = metallic.GetConnection();
				if (metallic_connection.has_value())
				{
					std::string path = metallic_connection.value().GetPrimPart();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					auto file = tex->file.GetValue().value().value;
					material_mid.metallic_tex = file.GetAssetPath();
				}
				else
				{
					material_mid.metallic = metallic.GetValue().value;
				}
			}
			{
				auto roughness = surface->roughness;
				auto roughness_connection = roughness.GetConnection();
				if (roughness_connection.has_value())
				{
					std::string path = roughness_connection.value().GetPrimPart();
					const tinyusdz::Prim* pshader1 = stage.GetPrimAtPath(tinyusdz::Path(path, "")).value();
					auto* shader1 = pshader1->data().as<tinyusdz::Shader>();
					auto* tex = shader1->value.as<tinyusdz::UsdUVTexture>();
					auto file = tex->file.GetValue().value().value;
					material_mid.roughness_tex = file.GetAssetPath();
				}
				else
				{
					material_mid.roughness = roughness.GetValue().value;
				}

			}

			int idx = (int)material_lst.size();
			material_lst.push_back(material_mid);
			material_map[path] = idx;

		}
		else if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_GEOM_MESH)
		{
			auto* mesh_in = prim.prim->data().as<tinyusdz::GeomMesh>();
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
				scene_out.nodes.push_back(node_id);
			}

			tinygltf::Mesh mesh_out;
			mesh_out.name = node_out.name;
			mesh_out.primitives.resize(1);

			auto& prim_out = mesh_out.primitives[0];

			int idx_material = -1;
			if (mesh_in->materialBinding.has_value())
			{
				idx_material = material_map[mesh_in->materialBinding.value().binding.full_path_name()];
			}
			else
			{
				idx_material = (int)material_lst.size();
				Mid::Material material_mid;
				auto iter = mesh_in->props.find("primvars:displayColor");
				if (iter != mesh_in->props.end())
				{
					auto col = iter->second.GetAttrib().get_value<std::vector<tinyusdz::value::float3>>().value()[0];
					material_mid.diffuse_color = { col[0], col[1], col[2] };
				}
				material_lst.push_back(material_mid);
			}

			Mid::Material& material_mid = material_lst[idx_material];
			material_mid.double_sided = mesh_in->doubleSided.GetValue();

			prim_out.material = idx_material;
			
			auto points = mesh_in->points.GetValue().value().value;
			auto extent = mesh_in->extent.GetValue().value().value;

			offset = buf_out.data.size();
			length = points.size() * sizeof(glm::vec3);
			buf_out.data.resize(offset + length);
			memcpy(buf_out.data.data() + offset, points.data(), length);

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
				acc.count = points.size();
				acc.minValues = { extent.lower[0], extent.lower[1], extent.lower[2] };
				acc.maxValues = { extent.upper[0], extent.upper[1], extent.upper[2] };
				m_out.accessors.push_back(acc);
			}

			prim_out.attributes["POSITION"] = acc_id;

			if (mesh_in->normals.GetValue().has_value())
			{
				auto norms = mesh_in->normals.GetValue().value().value;

				offset = buf_out.data.size();
				length = norms.size() * sizeof(glm::vec3);
				buf_out.data.resize(offset + length);
				memcpy(buf_out.data.data() + offset, norms.data(), length);

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
					acc.count = norms.size();
					m_out.accessors.push_back(acc);
				}

				prim_out.attributes["NORMAL"] = acc_id;
			}

			auto faceVertexIndices = mesh_in->faceVertexIndices.GetValue().value().value;
			auto faceVertexCounts = mesh_in->faceVertexCounts.GetValue().value().value;

			size_t idx_ind = 0;
			std::vector<glm::ivec3> faces;
			for (size_t i = 0; i < faceVertexCounts.size(); i++)
			{
				int count = faceVertexCounts[i];
				if (count == 3)
				{
					glm::ivec3 face;
					face.x = faceVertexIndices[idx_ind]; idx_ind++;
					face.y = faceVertexIndices[idx_ind]; idx_ind++;
					face.z = faceVertexIndices[idx_ind]; idx_ind++;
					faces.push_back(face);
				}
				else if (count == 4)
				{
					glm::ivec3 face1;
					face1.x = faceVertexIndices[idx_ind]; idx_ind++;
					face1.y = faceVertexIndices[idx_ind]; idx_ind++;
					face1.z = faceVertexIndices[idx_ind]; idx_ind++;
					faces.push_back(face1);

					glm::ivec3 face2;
					face2.x = face1.z;
					face2.y = faceVertexIndices[idx_ind]; idx_ind++;
					face2.z = face1.x;
					faces.push_back(face2);
				}
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


			auto iter = mesh_in->props.find("primvars:st0");
			if (iter != mesh_in->props.end())
			{
				auto st0 = iter->second.GetAttrib().get_value<std::vector<tinyusdz::value::float2>>().value();
				
				offset = buf_out.data.size();
				length = st0.size() * sizeof(glm::vec2);
				buf_out.data.resize(offset + length);
				memcpy(buf_out.data.data() + offset, st0.data(), length);

				float* p_uv = (float*)(buf_out.data.data() + offset);
				for (size_t i = 0; i < st0.size(); i++)
				{
					p_uv[i * 2 + 1] = 1.0f - p_uv[i * 2 + 1];
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
					acc.count = st0.size();
					m_out.accessors.push_back(acc);
				}

				prim_out.attributes["TEXCOORD_0"] = acc_id;
			}

			auto iter_ji = mesh_in->props.find("primvars:skel:jointIndices");
			auto iter_jw = mesh_in->props.find("primvars:skel:jointWeights");
			if (iter_ji != mesh_in->props.end() && iter_jw != mesh_in->props.end())
			{
				m_out.nodes[node_id].skin = 0;
				auto ji = iter_ji->second.GetAttrib().get_value<std::vector<int>>().value();
				auto jw = iter_jw->second.GetAttrib().get_value<std::vector<float>>().value();

				unsigned elem_size = iter_ji->second.GetAttrib().meta.elementSize.value();
				size_t count = ji.size() / elem_size;				

				std::vector<glm::u8vec4> conv_ji(count, { 0,0,0,0 });
				std::vector<glm::vec4> conv_jw(count, { 0.0f, 0.0f, 0.0f, 0.0f });
				
				for (size_t i = 0; i < count; i++)
				{
					unsigned elems = 4;
					if (elem_size < elems) elems = elem_size;
					for (unsigned j = 0; j < elems; j++)
					{
						size_t idx = elem_size * i + j;						
						conv_ji[i][j] = (uint8_t)ji[idx];
						conv_jw[i][j] = jw[idx];
					}
				}
				
				offset = buf_out.data.size();
				length = sizeof(glm::u8vec4) * count;
				buf_out.data.resize(offset + length);
				memcpy(buf_out.data.data() + offset, conv_ji.data(), length);

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
				memcpy(buf_out.data.data() + offset, conv_jw.data(), length);

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

			
			prim_out.mode = TINYGLTF_MODE_TRIANGLES;
			m_out.meshes.push_back(mesh_out);

		}

		else if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_SKEL_ROOT)
		{
			auto* skel_root_in = prim.prim->data().as<tinyusdz::SkelRoot>();
		}
		else if (prim.prim->data().type_id() == tinyusdz::value::TYPE_ID_SKELETON)
		{
			int skin_idx = (int)m_out.skins.size();
			m_out.skins.resize(skin_idx + 1);
			tinygltf::Skin& skin_out = m_out.skins[skin_idx];
			// skin_out.skeleton = prim.id_node_base;

			auto* skel_in = prim.prim->data().as<tinyusdz::Skeleton>();			
			auto bindTrans = skel_in->bindTransforms.GetValue().value();
			auto joints = skel_in->joints.GetValue().value();
			auto restTrans = skel_in->restTransforms.GetValue().value();		

			std::vector<glm::mat4> inv_binding_matrices(bindTrans.size());

			for (size_t i = 0; i < joints.size(); i++)
			{
				int node_id = (int)m_out.nodes.size();
				skin_out.joints.push_back(node_id);

				std::string path = joints[i].str();
				skeleton_map[path] = node_id;

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
				node_out.matrix.resize(16);
				memcpy(node_out.matrix.data(), &rest, sizeof(double) * 16);

				size_t pos = path.rfind('/');
				if (pos == std::string::npos)
				{
					node_out.name = path;
					m_out.nodes[prim.id_node_base].children.push_back(node_id);					
				}
				else
				{
					node_out.name = path.substr(pos + 1);
					int id_parent = skeleton_map[path.substr(0, pos)];
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

		if (prim.prim->data().type_id() != tinyusdz::value::TYPE_ID_MATERIAL)
		{
			int id_node_base = (int)(m_out.nodes.size() - 1);
			size_t num_children = prim.prim->children().size();
			for (size_t i = 0; i < num_children; i++)
			{
				queue_prim.push({ &prim.prim->children()[i],id_node_base, path });
			}
		}
	}

	std::vector<Mid::Image> tex_lst;
	std::unordered_map<std::string, int> tex_map;

	for (size_t i = 0; i < material_lst.size(); i++)
	{
		auto& material = material_lst[i];
		if (material.diffuse_tex!="")
		{
			auto& iter = tex_map.find(material.diffuse_tex);
			if (iter == tex_map.end())
			{
				int idx = (int)tex_lst.size();
				tex_lst.resize(idx + 1);

				Mid::Image& img = tex_lst[idx];				
				img.Load(material.diffuse_tex.c_str());
			
				tex_map[material.diffuse_tex] = idx;
				material.idx_diffuse = idx;
			}
			else
			{
				material.idx_diffuse = iter->second;
			}
			
		}
		if (material.emissive_tex != "")
		{
			auto& iter = tex_map.find(material.emissive_tex);
			if (iter == tex_map.end())
			{
				int idx = (int)tex_lst.size();
				tex_lst.resize(idx + 1);

				Mid::Image& img = tex_lst[idx];				
				img.Load(material.emissive_tex.c_str());

				tex_map[material.emissive_tex] = idx;
				material.idx_emissive = idx;
			}
			else
			{
				material.idx_emissive = iter->second;
			}
		}

		if (material.metallic_tex != "" || material.roughness_tex != "")
		{
			Mid::Image img_metallic, img_roughness;
			if (material.metallic_tex != "")
			{
				img_metallic.Load(material.metallic_tex.c_str());
			}
			if (material.roughness_tex != "")
			{
				img_roughness.Load(material.roughness_tex.c_str());
			}

			int idx = (int)tex_lst.size();
			tex_lst.resize(idx + 1);

			Mid::Image& img = tex_lst[idx];
			img.CreateMR(img_metallic, img_roughness);

			material.idx_metallic_roughness = idx;

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
		buf_out.data.resize(offset + length);
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
		
		if (material_mid.idx_diffuse>=0)
		{
			material_out.pbrMetallicRoughness.baseColorTexture.index = material_mid.idx_diffuse;
		}
		else
		{
			material_out.pbrMetallicRoughness.baseColorFactor = { material_mid.diffuse_color[0], material_mid.diffuse_color[1], material_mid.diffuse_color[2], 1.0f };
		}

		if (material_mid.idx_emissive>=0)
		{
			material_out.emissiveTexture.index = material_mid.idx_emissive;
		}
		else
		{
			material_out.emissiveFactor = { material_mid.emissive_color[0], material_mid.emissive_color[1], material_mid.emissive_color[2] };
		}

		if (material_mid.idx_metallic_roughness >= 0)
		{
			material_out.pbrMetallicRoughness.metallicRoughnessTexture.index = material_mid.idx_metallic_roughness;
			material_out.pbrMetallicRoughness.metallicFactor = 1.0f;
			material_out.pbrMetallicRoughness.roughnessFactor = 1.0f;
		}
		else
		{
			material_out.pbrMetallicRoughness.metallicFactor = material_mid.metallic;
			material_out.pbrMetallicRoughness.roughnessFactor = material_mid.roughness;
		}
	}

	tinygltf::TinyGLTF gltf;
	gltf.WriteGltfSceneToFile(&m_out, argv[2], true, true, false, true);

	return 0;
}
