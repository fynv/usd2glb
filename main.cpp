#include <cstdio>
#include <tinyusdz.hh>
#include <usda-writer.hh>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <glm.hpp>
#include <queue>

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

	tinyusdz::usda::SaveAsUSDA("output.udsa", stage, &warn, &err);

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

	struct Prim
	{
		tinyusdz::Prim* prim;
		int id_node_base = -1;
	};

	std::queue<Prim> queue_prim;
	queue_prim.push({ root_prim, -1 });

	while (!queue_prim.empty())
	{
		Prim prim = queue_prim.front();
		queue_prim.pop();

		if (prim.prim->data.type_id() == tinyusdz::value::TYPE_ID_GEOM_XFORM)
		{
			auto* node_in = prim.prim->data.as<tinyusdz::Xform>();
			int node_id = (int)m_out.nodes.size();

			tinygltf::Node node_out;
			node_out.name = node_in->name;	

			size_t num_ops = node_in->xformOps.size();
			if (num_ops > 0)
			{
				if (node_in->xformOps[0].op == tinyusdz::XformOp::OpType::Translate
					&& node_in->xformOps[0].suffix=="")
				{
					auto trans = node_in->xformOps[0].get_scalar_value<tinyusdz::value::double3>().value();
					node_out.translation = { trans[0], trans[1], trans[2] };
				}
			}			

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
		else if (prim.prim->data.type_id() == tinyusdz::value::TYPE_ID_GEOM_MESH)
		{
			auto* mesh_in = prim.prim->data.as<tinyusdz::GeomMesh>();
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

			int idx_material = (int)m_out.materials.size();
			{
				tinygltf::Material material_out;
				auto iter = mesh_in->props.find("primvars:displayColor");
				if (iter != mesh_in->props.end())
				{
					auto col = iter->second.attrib.get_value<std::vector<tinyusdz::value::float3>>().value()[0];
					material_out.pbrMetallicRoughness.baseColorFactor = { col[0], col[1], col[2], 1.0f };
				}
				m_out.materials.push_back(material_out);
			}
			prim_out.material = idx_material;

			auto points = mesh_in->points.GetValue().value().value;
			auto faceVertexIndices = mesh_in->faceVertexIndices.GetValue().value().value;
			auto faceVertexCounts = mesh_in->faceVertexCounts.GetValue().value().value;
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
			prim_out.mode = TINYGLTF_MODE_TRIANGLES;

			m_out.meshes.push_back(mesh_out);
		}

		int id_node_base = (int)(m_out.nodes.size() - 1);
		size_t num_children = prim.prim->children.size();
		for (size_t i = 0; i < num_children; i++)
		{
			queue_prim.push({ &prim.prim->children[i],id_node_base });
		}
	}

	tinygltf::TinyGLTF gltf;
	gltf.WriteGltfSceneToFile(&m_out, argv[2], true, true, false, true);

	return 0;
}
