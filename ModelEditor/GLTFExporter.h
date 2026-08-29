///////////
// This file is a part of the ATools project
// Some parts of code are the property of Microsoft, Qt or Aeonsoft
// The rest is released without license and without any warranty
///////////
// AI generated

#ifndef GLTFEXPORTER_H
#define GLTFEXPORTER_H

#include "Exporter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QVector>

class CMotion;

class CGLTFExporter : public CExporter
{
public:
	CGLTFExporter(CAnimatedMesh* mesh, bool exportAllLODs);

	virtual bool Export(const string& filename);

	// Exports every motion in `motions` (name -> preloaded .ani) as its own
	// named glTF "animation" entry against the same geometry/skeleton, built
	// only once. Each entry becomes a separate Action on import in Blender -
	// unlike COLLADA's library_animation_clips, which Blender's importer
	// flattens into a single action, so this is the multi-motion path that
	// actually works there. Passing an empty map behaves like Export().
	bool ExportAllMotions(const QMap<QString, CMotion*>& motions, const string& prefix, const string& filename);

private:
	QJsonArray m_jBuffers;
	QJsonArray m_jBufferViews;
	QJsonArray m_jAccessors;
	QJsonArray m_jMeshes;
	QJsonArray m_jMaterials;
	QJsonArray m_jImages;
	QJsonArray m_jTextures;
	QJsonArray m_jSamplers;
	QJsonArray m_jNodes;
	QJsonArray m_jSkins;
	QJsonArray m_jAnimations;

	QByteArray m_binBuffer;

	// name (same keys as m_objects) / Bone* -> node index in m_jNodes
	QMap<string, int> m_objectNodeIndices;
	QMap<Bone*, int> m_boneNodeIndices;
	QMap<Material*, int> m_materialIndices;
	// object name -> mesh index in m_jMeshes (only meaningful for non-collision,
	// non-empty objects; the collision object and skin objects are still meshes)
	QMap<string, int> m_meshIndices;
	// object name -> index in m_jSkins, filled by _writeSkins()
	QMap<string, int> m_skinIndices;

	int m_rootBoneNodeIndex;

	void _writeAsset(QJsonObject& root);
	void _writeImagesAndTextures();
	void _writeMaterials();
	void _writeMeshes();
	// Requires m_boneNodeIndices to already be populated (called from within
	// _writeNodesAndScene, after the bone-node pass).
	void _writeSkins();
	// Builds ONE glTF "animation" object from the current state of
	// m_animations / m_frameCount (as gathered by CExporter::_gatherAnimations)
	// and appends it to m_jAnimations, named clipName. Called once by
	// Export(), or once per motion by ExportAllMotions().
	void _writeAnimationClip(const QString& clipName);
	// Builds every node (bones, then objects) in flat passes, then wires up
	// parent/child relationships and the single default scene once every
	// index is known - avoids the chicken-and-egg problem of a skin needing
	// bone-node indices while an object node needs its skin index.
	void _writeNodesAndScene(QJsonObject& root);

	// Raw copy of data into m_binBuffer (4-byte aligned), registers a
	// bufferView and returns its index. target is an optional GL buffer
	// target constant (ARRAY_BUFFER / ELEMENT_ARRAY_BUFFER), 0 if not applicable.
	int _pushBufferView(const void* data, int byteLength, int target = 0);
	// Registers an accessor pointing at bufferView, returns its index.
	int _pushAccessor(int bufferView, int componentType, const QString& type, int count,
		const QJsonArray& min = QJsonArray(), const QJsonArray& max = QJsonArray());

	// Same handedness flip as CDAEExporter::_matToString (S * M * S with
	// S = diag(1,1,-1,1)), kept as a matrix so it can be decomposed below
	// instead of serialized to text.
	D3DXMATRIX _convertHandedness(const D3DXMATRIX& mat) const;

	// Writes a D3D matrix as a glTF matrix. The handedness-converted D3D
	// matrix is transposed implicitly by glTF's column-major matrix storage.
	void _writeTRS(QJsonObject& node, const D3DXMATRIX& mat) const;

	D3DXMATRIX _boneRestTM(Bone* bone) const;

	string _boneID(Bone* bone) const;

	bool _serialize(const string& filename, QJsonObject& root);
};

#endif // GLTFEXPORTER_H