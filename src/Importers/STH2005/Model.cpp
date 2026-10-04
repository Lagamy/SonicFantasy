#include "Model.h"
#include "Bon.h"
#include "Dff.h"
#include "GPU_Globals.h"
#include <format>
#include <stdexcept>

namespace STH2005
{
static int32_t findFrame(const Graphics::RenderWareMesh& rMesh_, const std::string& rName_)
{
	for(size_t i = 0; i < rMesh_.frames.size(); i++)
	{
		if(rMesh_.frames[i].name == rName_) return static_cast<int32_t>(i);
	}
	return -1;
}

/*
Names DFF frames from the .BON: a frame takes the name of the .BON node whose id equals its HAnim node id
(observed: ids and positions line up for every bone of SHADOW_BODY.DFF/SH.BON and SHADOW_HAND_L.DFF/LTE.BON).
Frames without a HAnim id (the clump root) stay unnamed.
Outputs the part's root bone name and its frame index (-1 if the part has no frame of that name).

Parts without a .BON (eyes SHADOW_EYE_L/R.DFF, eyelids SHADOW_MABUTA01-04.DFF) have only the clump root and one
identity frame (HAnim id 0) holding their single atomic, and no skin. That atomic's frame becomes the root, attached to
rPart_.bone. Hypothesis, unverified: the game draws them in sh_j_head's local space. Evidence: no file names a bone, but
offset by sh_j_head (+1.35 Y from sh_root, no rotation) their vertices lie on SHADOW_BODY.DFF's face surface
(median Z gap 0.13 for EYE_L, 0.02 for MABUTA04), while in sh_root space they float about 0.75 in front of it.
*/
static Graphics::RenderWareMesh readNamedPart(const ONE& rArchive_, const ModelPart& rPart_, std::string& rRootName_, int32_t& rRootFrame_)
{
	Graphics::RenderWareMesh mesh = Renderware::readDff(rArchive_.readFile(rPart_.dff));
	if(rPart_.bon == nullptr)
	{
		if(rPart_.bone == nullptr || mesh.atomics.empty())
		{
			throw std::runtime_error(std::format("STH2005::Model: {} has no .BON, so it needs a bone to attach to and an atomic.", rPart_.dff));
		}
		rRootName_ = rPart_.bone;
		rRootFrame_ = mesh.atomics[0].frame;
		return mesh;
	}

	Bon bon = readBon(rArchive_.readFile(rPart_.bon));
	if(bon.nodes.empty())
	{
		throw std::runtime_error(std::format("STH2005::Model: {} has no bones.", rPart_.bon));
	}

	for(Graphics::RenderWareFrame& rFrame : mesh.frames)
	{
		for(const BonNode& rNode : bon.nodes)
		{
			if(rFrame.boneId >= 0 && static_cast<uint32_t>(rFrame.boneId) == rNode.id)
			{
				rFrame.name = rNode.name;
			}
		}
	}
	rRootName_ = bon.nodes[0].name;
	rRootFrame_ = findFrame(mesh, rRootName_);
	return mesh;
}

/*
Attaches a part by bone name. Hypothesis, unverified: this is how the game places parts. Evidence: the root bone
of LTE.BON is named sh_j_l1te, the left hand bone of SH.BON (the ids differ: 70 vs 28), and likewise RTE.BON / sh_j_r1te.
The part's root frame (partRoot_, from readNamedPart) is replaced by the model's frame named rRootName_; frames below it are appended with remapped
parents (parents still come before children); frames above it (the part's clump root) are dropped.
The part's atomics are appended with remapped frame indices, and so are their skin bones' frames (a skin bone on a dropped
frame throws).
*/
static void attachPart(Graphics::RenderWareMesh& rModel_, Graphics::RenderWareMesh& rPart_, int32_t partRoot_, const std::string& rRootName_, const char* partName_)
{
	int32_t anchor = findFrame(rModel_, rRootName_);
	if(partRoot_ < 0 || anchor < 0)
	{
		throw std::runtime_error(std::format("STH2005::Model: {} root bone {} has no matching bone in the main model.", partName_, rRootName_));
	}

	std::vector<int32_t> remap(rPart_.frames.size(), -1);
	remap[partRoot_] = anchor;
	for(size_t i = partRoot_ + 1; i < rPart_.frames.size(); i++)
	{
		Graphics::RenderWareFrame frame = rPart_.frames[i];
		if(frame.parent < 0 || remap[frame.parent] < 0) continue;
		frame.parent = remap[frame.parent];
		remap[i] = static_cast<int32_t>(rModel_.frames.size());
		rModel_.frames.push_back(frame);
	}

	for(Graphics::RenderWareAtomic& rAtomic : rPart_.atomics)
	{
		if(remap[rAtomic.frame] < 0)
		{
			throw std::runtime_error(std::format("STH2005::Model: {} has an atomic outside its root bone {}.", partName_, rRootName_));
		}
		rAtomic.frame = remap[rAtomic.frame];
		for(int32_t& rBoneFrame : rAtomic.skinBoneFrames)
		{
			if(remap[rBoneFrame] < 0)
			{
				throw std::runtime_error(std::format("STH2005::Model: {} has a skin bone outside its root bone {}.", partName_, rRootName_));
			}
			rBoneFrame = remap[rBoneFrame];
		}
		rModel_.atomics.push_back(rAtomic);
	}
}

uint32_t importModel(const char* name_, ONE& rArchive_, const std::vector<ModelPart>& rParts_)
{
	if(rParts_.empty())
	{
		throw std::runtime_error(std::format("STH2005::Model: {} has no parts.", name_));
	}

	/*
	A main part without a .BON is a static model (stage objects such as RING.DFF have none): its frames are kept as the DFF has them, unnamed, so no other
	part can attach to it.
	*/
	if(rParts_[0].bon == nullptr)
	{
		if(rParts_.size() != 1)
		{
			throw std::runtime_error(std::format("STH2005::Model: main part {} of {} has no .BON, so other parts can't attach to it.", rParts_[0].dff, name_));
		}
		return rArchive_.addMesh(name_, Renderware::readDff(rArchive_.readFile(rParts_[0].dff)), rParts_);
	}

	std::string rootName;
	int32_t rootFrame;
	Graphics::RenderWareMesh model = readNamedPart(rArchive_, rParts_[0], rootName, rootFrame);
	for(size_t p = 1; p < rParts_.size(); p++)
	{
		Graphics::RenderWareMesh part = readNamedPart(rArchive_, rParts_[p], rootName, rootFrame);
		attachPart(model, part, rootFrame, rootName, rParts_[p].dff);
	}

	return rArchive_.addMesh(name_, std::move(model), rParts_);
}
}
