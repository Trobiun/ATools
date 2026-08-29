///////////
// This file is a part of the ATools project
// Some parts of code are the property of Microsoft, Qt or Aeonsoft
// The rest is released without license and without any warranty
///////////

#ifndef EXPORTER_H
#define EXPORTER_H

class CAnimatedMesh;
struct GMObject;
struct Material;
struct Bone;
struct TMAnimation;
struct MaterialBlock;

class CExporter
{
public:
	CExporter(CAnimatedMesh* mesh, bool exportAllLODs);

	virtual bool Export(const string& filename) = 0;

protected:
	int m_frameCount;
	string m_rootBoneID;
	QMap<string, Material*> m_materials;
	QMap<Material*, MaterialBlock*> m_materialBlocks;
	QList<Bone*> m_bones;
	QMap<string, GMObject*> m_objects;
	QMap<string, TMAnimation*> m_animations;
	QMap<Bone*, int> m_boneIDs;
	QMap<GMObject*, int> m_objectIDs;
	QMap<GMObject*, int> m_objectLODs;
	QMap<Bone*, D3DXMATRIX> m_boneAnimTMs;

	// Kept so a subclass can re-scan the animation data after the caller
	// swaps mesh->m_motion for a different preloaded .ani (see PlayMotion),
	// without rebuilding the geometry/materials/skeleton maps above.
	CAnimatedMesh* m_mesh;
	CObject3D* m_obj3D;

	void _gatherAnimations();

	string _getMaterialID(Material* mat);
	MaterialBlock* _getMaterialBlock(Material* mat);

	D3DXMATRIX _getRotationMatrix(const D3DXQUATERNION& quat) const;

};

#endif // EXPORTER_H