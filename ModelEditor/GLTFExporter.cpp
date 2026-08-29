///////////
// This file is a part of the ATools project
// Some parts of code are the property of Microsoft, Qt or Aeonsoft
// The rest is released without license and without any warranty
///////////
// AI generated

#include "stdafx.h"
#include "GLTFExporter.h"
#include "AnimatedMesh.h"
#include <Object3D.h>
#include <Motion.h>
#include <QFile>
#include <QFileInfo>

// glTF / OpenGL constants used below (kept local instead of pulling in a GL header).
static const int GLTF_BYTE = 5120;
static const int GLTF_UNSIGNED_BYTE = 5121;
static const int GLTF_UNSIGNED_SHORT = 5123;
static const int GLTF_FLOAT = 5126;
static const int GLTF_ARRAY_BUFFER = 34962;
static const int GLTF_ELEMENT_ARRAY_BUFFER = 34963;
static const int GLTF_TRIANGLES = 4;

CGLTFExporter::CGLTFExporter(CAnimatedMesh* mesh, bool exportAllLODs)
	: CExporter(mesh, exportAllLODs)
	, m_rootBoneNodeIndex(-1)
{
}

bool CGLTFExporter::Export(const string& filename)
{
	return ExportAllMotions(QMap<QString, CMotion*>(), "", filename);
}

bool CGLTFExporter::ExportAllMotions(const QMap<QString, CMotion*>& motions, const QString& prefix, const string& filename)
{
	_writeImagesAndTextures();
	_writeMaterials();
	_writeMeshes();

	QJsonObject root;

	// Builds every node (bones + objects), wires up parents/children, sets
	// root["nodes"] / root["scenes"] / root["scene"], and internally calls
	// _writeSkins() once bone-node indices exist. Geometry/materials/skeleton
	// are built exactly once, regardless of how many motions are exported.
	_writeNodesAndScene(root);

	if (motions.isEmpty())
	{
		// Single-clip path: export whatever motion is already attached to
		// m_mesh/m_obj3D (gathered by the constructor).
		_writeAnimationClip(QString("Motion"));
	}
	else
	{
		for (auto it = motions.begin(); it != motions.end(); it++)
		{
			// Same hot-swap CMainFrame::PlayMotion performs: point the mesh
			// at a different preloaded .ani, then re-run just the animation
			// gathering step - geometry/materials/skeleton above stay as-is.
			// Each call appends its own accessors/bufferViews, so - unlike
			// COLLADA - clip names never need to be baked into any ID to
			// avoid collisions.
			m_mesh->SetMotion(it.key());
			_gatherAnimations();
			QString newClipName(it.key());
			newClipName.remove(prefix + "_", Qt::CaseInsensitive).remove(".ani", Qt::CaseInsensitive);
			_writeAnimationClip(newClipName);
		}
		m_mesh->SetMotion("");
		m_animations.clear();
		m_boneAnimTMs.clear();
	}

	_writeAsset(root);

	if (!m_jImages.isEmpty())     root["images"] = m_jImages;
	if (!m_jSamplers.isEmpty())   root["samplers"] = m_jSamplers;
	if (!m_jTextures.isEmpty())   root["textures"] = m_jTextures;
	if (!m_jMaterials.isEmpty())  root["materials"] = m_jMaterials;
	if (!m_jMeshes.isEmpty())     root["meshes"] = m_jMeshes;
	if (!m_jSkins.isEmpty())      root["skins"] = m_jSkins;
	if (!m_jAnimations.isEmpty()) root["animations"] = m_jAnimations;
	root["accessors"] = m_jAccessors;
	root["bufferViews"] = m_jBufferViews;

	return _serialize(filename, root);
}

void CGLTFExporter::_writeAsset(QJsonObject& root)
{
	QJsonObject asset;
	asset["version"] = QString("2.0");
	asset["generator"] = QString("ATools GLTFExporter");
	root["asset"] = asset;
}

void CGLTFExporter::_writeImagesAndTextures()
{
	int idx = 0;
	for (auto it = m_materials.begin(); it != m_materials.end(); it++, idx++)
	{
		QJsonObject image;
		image["uri"] = QString(it.value()->textureName);
		m_jImages.append(image);

		QJsonObject sampler;
		sampler["magFilter"] = 9729; // LINEAR
		sampler["minFilter"] = 9987; // LINEAR_MIPMAP_LINEAR
		sampler["wrapS"] = 10497;    // REPEAT
		sampler["wrapT"] = 10497;
		m_jSamplers.append(sampler);

		QJsonObject texture;
		texture["source"] = idx;
		texture["sampler"] = idx;
		m_jTextures.append(texture);
	}
}

void CGLTFExporter::_writeMaterials()
{
	int idx = 0;
	for (auto it = m_materials.begin(); it != m_materials.end(); it++, idx++)
	{
		QJsonObject material;
		material["name"] = QString(it.key());

		QJsonObject pbr;
		QJsonObject baseColorTexture;
		baseColorTexture["index"] = idx;
		baseColorTexture["texCoord"] = 0;
		pbr["baseColorTexture"] = baseColorTexture;
		// No metallic/roughness data in the source Phong material - flatten
		// to a fully diffuse, non-metallic look, same intent as the fixed
		// ambient/specular CDAEExporter bakes into every <phong> block.
		pbr["metallicFactor"] = 0.0;
		pbr["roughnessFactor"] = 1.0;

		MaterialBlock* block = _getMaterialBlock(it.value());
		if (block)
		{
			if (block->effect & XE_2SIDE)
				material["doubleSided"] = true;

			if (block->effect & XE_OPACITY)
			{
				QJsonArray baseColorFactor;
				baseColorFactor.append(1.0);
				baseColorFactor.append(1.0);
				baseColorFactor.append(1.0);
				baseColorFactor.append((double)block->amount / 255.0);
				pbr["baseColorFactor"] = baseColorFactor;
				material["alphaMode"] = QString("BLEND");
			}
		}

		material["pbrMetallicRoughness"] = pbr;
		m_jMaterials.append(material);

		m_materialIndices[it.value()] = idx;
	}
}

void CGLTFExporter::_writeMeshes()
{
	for (auto it = m_objects.begin(); it != m_objects.end(); it++)
	{
		GMObject* obj = it.value();
		const string name = it.key();
		const int vertexCount = obj->vertexCount;

		if (vertexCount == 0)
			continue;

		QVector<D3DXVECTOR3> positions(vertexCount);
		QVector<D3DXVECTOR3> normals(vertexCount);
		QVector<float> uvs(vertexCount * 2);

		D3DXVECTOR3 pMin(3.4e38f, 3.4e38f, 3.4e38f);
		D3DXVECTOR3 pMax(-3.4e38f, -3.4e38f, -3.4e38f);

		for (int i = 0; i < vertexCount; i++)
		{
			D3DXVECTOR3 p, n;
			D3DXVECTOR2 t;

			if (obj->type == GMT_SKIN)
			{
				SkinVertex& v = ((SkinVertex*)obj->vertices)[i];
				p = v.p; n = v.n; t = v.t;
			}
			else
			{
				NormalVertex& v = ((NormalVertex*)obj->vertices)[i];
				p = v.p; n = v.n; t = v.t;
			}

			// Same left-handed -> right-handed flip CDAEExporter applies
			// (negate Z) so the mesh comes out the same way round in glTF.
			positions[i] = D3DXVECTOR3(p.x, p.y, -p.z);
			normals[i] = D3DXVECTOR3(n.x, n.y, -n.z);
			uvs[i * 2 + 0] = t.x;
			uvs[i * 2 + 1] = 1.0f - t.y;

			if (positions[i].x < pMin.x) pMin.x = positions[i].x;
			if (positions[i].y < pMin.y) pMin.y = positions[i].y;
			if (positions[i].z < pMin.z) pMin.z = positions[i].z;
			if (positions[i].x > pMax.x) pMax.x = positions[i].x;
			if (positions[i].y > pMax.y) pMax.y = positions[i].y;
			if (positions[i].z > pMax.z) pMax.z = positions[i].z;
		}

		QJsonObject attributes;

		int posView = _pushBufferView(positions.constData(), vertexCount * sizeof(D3DXVECTOR3), GLTF_ARRAY_BUFFER);
		QJsonArray minA, maxA;
		minA.append(pMin.x); minA.append(pMin.y); minA.append(pMin.z);
		maxA.append(pMax.x); maxA.append(pMax.y); maxA.append(pMax.z);
		attributes["POSITION"] = _pushAccessor(posView, GLTF_FLOAT, "VEC3", vertexCount, minA, maxA);

		int normView = _pushBufferView(normals.constData(), vertexCount * sizeof(D3DXVECTOR3), GLTF_ARRAY_BUFFER);
		attributes["NORMAL"] = _pushAccessor(normView, GLTF_FLOAT, "VEC3", vertexCount);

		int uvView = _pushBufferView(uvs.constData(), uvs.size() * sizeof(float), GLTF_ARRAY_BUFFER);
		attributes["TEXCOORD_0"] = _pushAccessor(uvView, GLTF_FLOAT, "VEC2", vertexCount);

		if (obj->type == GMT_SKIN)
		{
			// SkinVertex only carries 2 bone influences; JOINTS_0/WEIGHTS_0
			// are still vec4 per the spec, so the last two lanes stay at 0.
			QVector<quint8> joints(vertexCount * 4, 0);
			QVector<float> weights(vertexCount * 4, 0.0f);

			SkinVertex* vertices = (SkinVertex*)obj->vertices;

			int boneIds[MAX_BONES];
			for (int j = 0; j < MAX_BONES; j++)
				boneIds[j] = j;
			if (obj->usedBoneCount > 0)
				for (int j = 0; j < obj->usedBoneCount; j++)
					boneIds[j] = obj->usedBones[j];

			for (int i = 0; i < vertexCount; i++)
			{
				SkinVertex& v = vertices[i];
				if (v.w1 != 0.0f)
				{
					joints[i * 4 + 0] = (quint8)boneIds[v.id1 / 3];
					weights[i * 4 + 0] = v.w1;
				}
				if (v.w2 != 0.0f)
				{
					joints[i * 4 + 1] = (quint8)boneIds[v.id2 / 3];
					weights[i * 4 + 1] = v.w2;
				}
			}

			int jointsView = _pushBufferView(joints.constData(), joints.size() * sizeof(quint8), GLTF_ARRAY_BUFFER);
			attributes["JOINTS_0"] = _pushAccessor(jointsView, GLTF_UNSIGNED_BYTE, "VEC4", vertexCount);

			int weightsView = _pushBufferView(weights.constData(), weights.size() * sizeof(float), GLTF_ARRAY_BUFFER);
			attributes["WEIGHTS_0"] = _pushAccessor(weightsView, GLTF_FLOAT, "VEC4", vertexCount);
		}

		QJsonArray primitives;
		for (int b = 0; b < obj->materialBlockCount; b++)
		{
			MaterialBlock* block = &obj->materialBlocks[b];

			QVector<ushort> indices(block->primitiveCount * 3);
			for (int i = 0; i < block->primitiveCount * 3; i++)
				indices[i] = obj->indices[block->startVertex + i];

			int idxView = _pushBufferView(indices.constData(), indices.size() * sizeof(ushort), GLTF_ELEMENT_ARRAY_BUFFER);
			int idxAccessor = _pushAccessor(idxView, GLTF_UNSIGNED_SHORT, "SCALAR", indices.size());

			QJsonObject primitive;
			primitive["attributes"] = attributes;
			primitive["indices"] = idxAccessor;
			primitive["mode"] = GLTF_TRIANGLES;

			if (obj->material && block->materialID >= 0)
			{
				Material* mat = &obj->materials[block->materialID];
				if (m_materialIndices.contains(mat))
					primitive["material"] = m_materialIndices[mat];
			}

			primitives.append(primitive);
		}

		// Defensive fallback: every real object should have at least one
		// material block (CExporter's constructor synthesizes one for the
		// collision mesh when it has none), but if it somehow doesn't, still
		// emit a single primitive over the whole index buffer instead of an
		// invalid mesh with zero primitives.
		if (primitives.isEmpty() && obj->indexCount > 0)
		{
			QVector<ushort> indices(obj->indexCount);
			for (int i = 0; i < obj->indexCount; i++)
				indices[i] = obj->indices[i];

			int idxView = _pushBufferView(indices.constData(), indices.size() * sizeof(ushort), GLTF_ELEMENT_ARRAY_BUFFER);
			int idxAccessor = _pushAccessor(idxView, GLTF_UNSIGNED_SHORT, "SCALAR", indices.size());

			QJsonObject primitive;
			primitive["attributes"] = attributes;
			primitive["indices"] = idxAccessor;
			primitive["mode"] = GLTF_TRIANGLES;
			primitives.append(primitive);
		}

		if (primitives.isEmpty())
			continue;

		QJsonObject mesh;
		mesh["name"] = QString(name);
		mesh["primitives"] = primitives;

		m_meshIndices[name] = m_jMeshes.size();
		m_jMeshes.append(mesh);
	}
}

void CGLTFExporter::_writeSkins()
{
	// Requires m_boneNodeIndices to already be filled in (bone-node pass of
	// _writeNodesAndScene must run before this).
	for (auto it = m_objects.begin(); it != m_objects.end(); it++)
	{
		GMObject* obj = it.value();
		if (obj->type != GMT_SKIN)
			continue;

		const string name = it.key();

		QVector<int> jointNodeIndices(m_boneIDs.size());
		QVector<D3DXMATRIX> ibms(m_boneIDs.size());

		for (auto bit = m_boneIDs.begin(); bit != m_boneIDs.end(); bit++)
		{
			jointNodeIndices[bit.value()] = m_boneNodeIndices.value(bit.key(), 0);
			ibms[bit.value()] = _convertHandedness(bit.key()->inverseTM);
		}

		QJsonArray joints;
		for (int i = 0; i < jointNodeIndices.size(); i++)
			joints.append(jointNodeIndices[i]);

		// glTF matrices are column-major float[16] representing Mgl, and we
		// need Mgl = Md3d^T (D3D uses row-vector transforms, glTF uses
		// column-vector transforms). CDAEExporter::_matToString has to
		// transpose explicitly because it writes literal text meant to be
		// read back as-is. Here, the row-major *storage* of Md3d already
		// equals the column-major *storage* of Md3d^T - it's the same 16
		// floats in the same order - so this is a straight copy, NOT a
		// transpose. (An earlier version of this function re-applied
		// CDAEExporter's index swap here, which cancelled the conversion
		// out and produced a completely wrong bind pose.)
		QVector<float> ibmFloats(ibms.size() * 16);
		for (int i = 0; i < ibms.size(); i++)
			for (int k = 0; k < 16; k++)
				ibmFloats[i * 16 + k] = ibms[i][k];

		int ibmView = _pushBufferView(ibmFloats.constData(), ibmFloats.size() * sizeof(float));
		int ibmAccessor = _pushAccessor(ibmView, GLTF_FLOAT, "MAT4", ibms.size());

		QJsonObject skin;
		skin["joints"] = joints;
		skin["inverseBindMatrices"] = ibmAccessor;
		if (m_rootBoneNodeIndex >= 0)
			skin["skeleton"] = m_rootBoneNodeIndex;

		m_skinIndices[name] = m_jSkins.size();
		m_jSkins.append(skin);
	}
}

void CGLTFExporter::_writeNodesAndScene(QJsonObject& root)
{
	// --- Pass 1: flat bone nodes -------------------------------------------
	for (int i = 0; i < m_bones.size(); i++)
	{
		Bone* bone = m_bones[i];

		QJsonObject node;
		node["name"] = QString(_boneID(bone));

		const string animationID = _boneID(bone) % "-transform";
		auto animIt = m_animations.constFind(animationID);

		if (animIt != m_animations.constEnd() && animIt.value() && m_frameCount > 0)
		{
			TMAnimation* frame = animIt.value();
			D3DXMATRIX translation, rotation, local;

			D3DXMatrixTranslation(&translation,
				frame[0].pos.x, frame[0].pos.y, frame[0].pos.z);
			D3DXMatrixRotationQuaternion(&rotation, &frame[0].rot);

			local = rotation * translation;
			_writeTRS(node, _convertHandedness(local));
		}
		else
		{
			_writeTRS(node, _convertHandedness(_boneRestTM(bone)));
		}

		m_jNodes.append(node);
		int index = m_jNodes.size() - 1;
		m_boneNodeIndices[bone] = index;

		if (bone->parentID == -1)
			m_rootBoneNodeIndex = index;
	}

	// --- Pass 2: skins (needs bone nodes), then flat object nodes ----------
	_writeSkins();

	for (auto it = m_objects.begin(); it != m_objects.end(); it++)
	{
		GMObject* obj = it.value();
		const string name = it.key();

		QJsonObject node;
		node["name"] = QString(name);
		_writeTRS(node, _convertHandedness(obj->transform));

		if (m_meshIndices.contains(name))
			node["mesh"] = m_meshIndices[name];

		if (obj->type == GMT_SKIN && m_skinIndices.contains(name))
			node["skin"] = m_skinIndices[name];

		m_jNodes.append(node);
		m_objectNodeIndices[name] = m_jNodes.size() - 1;
	}

	// --- Pass 3: wire up children now every index exists --------------------
	QJsonArray sceneNodes;

	for (int i = 0; i < m_bones.size(); i++)
	{
		Bone* bone = m_bones[i];
		QJsonArray children;

		for (int j = 0; j < m_bones.size(); j++)
			if (m_bones[j]->parentID == m_boneIDs[bone])
				children.append(m_boneNodeIndices[m_bones[j]]);

		for (auto it = m_objects.begin(); it != m_objects.end(); it++)
			if (it.value()->parentID == m_boneIDs[bone] && it.value()->parentType == GMT_BONE)
				children.append(m_objectNodeIndices[it.key()]);

		if (!children.isEmpty())
		{
			QJsonObject node = m_jNodes[m_boneNodeIndices[bone]].toObject();
			node["children"] = children;
			m_jNodes.replace(m_boneNodeIndices[bone], node);
		}

		if (bone->parentID == -1)
			sceneNodes.append(m_boneNodeIndices[bone]);
	}

	for (auto it = m_objects.begin(); it != m_objects.end(); it++)
	{
		GMObject* obj = it.value();
		QJsonArray children;

		for (auto it2 = m_objects.begin(); it2 != m_objects.end(); it2++)
			if (it2.value()->parentID == m_objectIDs[obj]
				&& m_objectLODs[it2.value()] == m_objectLODs[obj]
				&& it2.value()->parentType != GMT_BONE)
				children.append(m_objectNodeIndices[it2.key()]);

		if (!children.isEmpty())
		{
			QJsonObject node = m_jNodes[m_objectNodeIndices[it.key()]].toObject();
			node["children"] = children;
			m_jNodes.replace(m_objectNodeIndices[it.key()], node);
		}

		if (obj->parentID == -1)
			sceneNodes.append(m_objectNodeIndices[it.key()]);
	}

	QJsonObject scene;
	scene["nodes"] = sceneNodes;

	QJsonArray scenes;
	scenes.append(scene);

	root["nodes"] = m_jNodes;
	root["scenes"] = scenes;
	root["scene"] = 0;
}

void CGLTFExporter::_writeAnimationClip(const QString& clipName)
{
	if (m_animations.isEmpty() || m_frameCount <= 0)
		return;

	// A bone with no dedicated per-frame track already got its correct rest
	// pose baked into its node's TRS in _writeNodesAndScene (via
	// _boneRestTM), so - unlike CDAEExporter's "silent joint" workaround -
	// it doesn't need a fake constant channel here: glTF importers evaluate
	// an un-animated joint using its node transform directly.

	QMap<string, int> nodeIndexByID;
	for (auto it = m_boneNodeIndices.begin(); it != m_boneNodeIndices.end(); it++)
		nodeIndexByID[_boneID(it.key())] = it.value();
	for (auto it = m_objectNodeIndices.begin(); it != m_objectNodeIndices.end(); it++)
		nodeIndexByID[it.key()] = it.value();

	QVector<float> times(m_frameCount);
	for (int i = 0; i < m_frameCount; i++)
		times[i] = (1.0f / 30.0f) * (float)i;

	int timeView = _pushBufferView(times.constData(), times.size() * sizeof(float));
	QJsonArray minT, maxT;
	minT.append(times.first());
	maxT.append(times.last());
	int timeAccessor = _pushAccessor(timeView, GLTF_FLOAT, "SCALAR", m_frameCount, minT, maxT);

	QJsonArray samplers;
	QJsonArray channels;

	for (auto it = m_animations.begin(); it != m_animations.end(); it++)
	{
		string targetID = it.key();
		targetID.remove(targetID.size() - 10, 10); // strip trailing "-transform"

		if (!nodeIndexByID.contains(targetID))
			continue;
		const int nodeIndex = nodeIndexByID[targetID];

		TMAnimation* frames = it.value();

		QVector<float> translations(m_frameCount * 3);
		QVector<float> rotations(m_frameCount * 4);

		for (int i = 0; i < m_frameCount; i++)
		{
			// Build the local D3D matrix used by the original engine:
			//     M = Rotation * Translation
			//
			// Then convert its handedness. glTF animation channels cannot
			// contain a matrix, so we decompose the converted D3D matrix.
			// _decomposeForGLTF converts the D3D quaternion to the equivalent
			// glTF column-vector quaternion.
			const D3DXVECTOR3& p = frames[i].pos;
			const D3DXQUATERNION& q = frames[i].rot;

			translations[i * 3 + 0] = p.x;
			translations[i * 3 + 1] = p.y;
			translations[i * 3 + 2] = -p.z;

			rotations[i * 4 + 0] = -q.x;
			rotations[i * 4 + 1] = -q.y;
			rotations[i * 4 + 2] = q.z;
			rotations[i * 4 + 3] = q.w;
		}

		int tView = _pushBufferView(translations.constData(), translations.size() * sizeof(float));
		int tAccessor = _pushAccessor(tView, GLTF_FLOAT, "VEC3", m_frameCount);

		int rView = _pushBufferView(rotations.constData(), rotations.size() * sizeof(float));
		int rAccessor = _pushAccessor(rView, GLTF_FLOAT, "VEC4", m_frameCount);

		QJsonObject tSampler;
		tSampler["input"] = timeAccessor;
		tSampler["output"] = tAccessor;
		tSampler["interpolation"] = QString("LINEAR");
		const int tSamplerIndex = samplers.size();
		samplers.append(tSampler);

		QJsonObject rSampler;
		rSampler["input"] = timeAccessor;
		rSampler["output"] = rAccessor;
		rSampler["interpolation"] = QString("LINEAR");
		const int rSamplerIndex = samplers.size();
		samplers.append(rSampler);

		QJsonObject tTarget;
		tTarget["node"] = nodeIndex;
		tTarget["path"] = QString("translation");
		QJsonObject tChannel;
		tChannel["sampler"] = tSamplerIndex;
		tChannel["target"] = tTarget;
		channels.append(tChannel);

		QJsonObject rTarget;
		rTarget["node"] = nodeIndex;
		rTarget["path"] = QString("rotation");
		QJsonObject rChannel;
		rChannel["sampler"] = rSamplerIndex;
		rChannel["target"] = rTarget;
		channels.append(rChannel);
	}

	// Add constant tracks for skeleton bones which have no motion track.
	// The original DAE exporter still emits these transforms for every frame;
	// doing the same here prevents Blender from treating these joints as
	// outside the animation.
	QSet<string> animatedBones;

	for (auto it = m_animations.begin(); it != m_animations.end(); it++)
	{
		string id = it.key();
		const string suffix = "-transform";

		if (id.size() >= suffix.size()
			&& id.mid(0, id.size() - suffix.size()) == suffix)
		{
			id.remove(id.size() - suffix.size(), suffix.size());
			animatedBones.insert(id);
		}
	}

	for (int b = 0; b < m_bones.size(); b++)
	{
		Bone* bone = m_bones[b];
		const string boneID = _boneID(bone);

		if (animatedBones.contains(boneID) || !nodeIndexByID.contains(boneID))
			continue;

		const int nodeIndex = nodeIndexByID[boneID];

		// Same local transform as the skeleton node.
		D3DXMATRIX local = _convertHandedness(_boneRestTM(bone));
		D3DXVECTOR3 scale, trans;
		D3DXQUATERNION rot;
		D3DXMATRIX copy = local;

		if (FAILED(D3DXMatrixDecompose(&scale, &rot, &trans, &copy)))
			continue;

		QVector<float> translations(m_frameCount * 3);
		QVector<float> rotations(m_frameCount * 4);

		for (int i = 0; i < m_frameCount; i++)
		{
			translations[i * 3 + 0] = trans.x;
			translations[i * 3 + 1] = trans.y;
			translations[i * 3 + 2] = trans.z;

			rotations[i * 4 + 0] = rot.x;
			rotations[i * 4 + 1] = rot.y;
			rotations[i * 4 + 2] = rot.z;
			rotations[i * 4 + 3] = rot.w;
		}

		int tView = _pushBufferView(translations.constData(), translations.size() * sizeof(float));
		int rView = _pushBufferView(rotations.constData(), rotations.size() * sizeof(float));

		int tAccessor = _pushAccessor(tView, GLTF_FLOAT, "VEC3", m_frameCount);
		int rAccessor = _pushAccessor(rView, GLTF_FLOAT, "VEC4", m_frameCount);

		QJsonObject tSampler;
		tSampler["input"] = timeAccessor;
		tSampler["output"] = tAccessor;
		tSampler["interpolation"] = QString("STEP");
		int tSamplerIndex = samplers.size();
		samplers.append(tSampler);

		QJsonObject rSampler;
		rSampler["input"] = timeAccessor;
		rSampler["output"] = rAccessor;
		rSampler["interpolation"] = QString("STEP");
		int rSamplerIndex = samplers.size();
		samplers.append(rSampler);

		QJsonObject tTarget;
		tTarget["node"] = nodeIndex;
		tTarget["path"] = QString("translation");
		QJsonObject tChannel;
		tChannel["sampler"] = tSamplerIndex;
		tChannel["target"] = tTarget;
		channels.append(tChannel);

		QJsonObject rTarget;
		rTarget["node"] = nodeIndex;
		rTarget["path"] = QString("rotation");
		QJsonObject rChannel;
		rChannel["sampler"] = rSamplerIndex;
		rChannel["target"] = rTarget;
		channels.append(rChannel);
	}

	if (channels.isEmpty())
		return;

	QJsonObject animation;
	animation["name"] = clipName;
	animation["samplers"] = samplers;
	animation["channels"] = channels;

	m_jAnimations.append(animation);
}

int CGLTFExporter::_pushBufferView(const void* data, int byteLength, int target)
{
	// bufferViews should start on a 4-byte boundary.
	while (m_binBuffer.size() % 4 != 0)
		m_binBuffer.append('\0');

	QJsonObject view;
	view["buffer"] = 0;
	view["byteOffset"] = m_binBuffer.size();
	view["byteLength"] = byteLength;
	if (target != 0)
		view["target"] = target;

	m_binBuffer.append((const char*)data, byteLength);

	m_jBufferViews.append(view);
	return m_jBufferViews.size() - 1;
}

int CGLTFExporter::_pushAccessor(int bufferView, int componentType, const QString& type, int count,
	const QJsonArray& min, const QJsonArray& max)
{
	QJsonObject accessor;
	accessor["bufferView"] = bufferView;
	accessor["componentType"] = componentType;
	accessor["type"] = type;
	accessor["count"] = count;
	if (!min.isEmpty())
		accessor["min"] = min;
	if (!max.isEmpty())
		accessor["max"] = max;

	m_jAccessors.append(accessor);
	return m_jAccessors.size() - 1;
}

D3DXMATRIX CGLTFExporter::_convertHandedness(const D3DXMATRIX& mat) const
{
	// Same S * M * S conjugation (S = diag(1,1,-1,1)) as
	// CDAEExporter::_matToString, kept as a matrix instead of text so it can
	// be decomposed into glTF's translation/rotation/scale.
	return D3DXMATRIX(
		mat._11, mat._12, -mat._13, mat._14,
		mat._21, mat._22, -mat._23, mat._24,
		-mat._31, -mat._32, mat._33, -mat._34,
		mat._41, mat._42, -mat._43, mat._44
	);
}

void CGLTFExporter::_writeTRS(QJsonObject& node, const D3DXMATRIX& mat) const
{
	// Do not decompose the source matrix here.
	//
	// The source engine uses D3D row-vector matrices. glTF uses
	// column-vector matrices. Therefore a D3D matrix M is represented by
	// M^T in glTF. glTF stores a matrix column-major, so the 16 floats of
	// the D3DXMATRIX can be copied in their existing order directly.
	//
	// Using a matrix also avoids introducing numerical errors and, more
	// importantly, avoids accidentally changing the local bone transform
	// when D3DXMatrixDecompose encounters mirrored/non-uniform transforms.
	QJsonArray matrix;
	for (int i = 0; i < 16; i++)
		matrix.append(mat[i]);

	node["matrix"] = matrix;
}

D3DXMATRIX CGLTFExporter::_boneRestTM(Bone* bone) const
{
	auto it = m_boneAnimTMs.constFind(bone);
	if (it == m_boneAnimTMs.constEnd())
		return bone->localTM;
	return it.value();
}

string CGLTFExporter::_boneID(Bone* bone) const
{
	return string(bone->name).toLower().replace('.', '_').replace('-', '_').replace(' ', '_');
}

bool CGLTFExporter::_serialize(const string& filename, QJsonObject& root)
{
	QFileInfo info(filename);
	const QString binFileName = info.completeBaseName() % ".bin";
	const QString binFilePath = info.absolutePath() % '/' % binFileName;

	QFile binFile(binFilePath);
	if (!binFile.open(QIODevice::WriteOnly))
		return false;
	binFile.write(m_binBuffer);
	binFile.close();

	QJsonObject buffer;
	buffer["uri"] = binFileName;
	buffer["byteLength"] = m_binBuffer.size();
	m_jBuffers.append(buffer);
	root["buffers"] = m_jBuffers;

	QFile file(filename);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;

	QJsonDocument doc(root);
	file.write(doc.toJson(QJsonDocument::Indented));
	file.close();

	return true;
}