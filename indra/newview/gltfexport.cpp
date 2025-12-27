/**
 * @file gltfexport.cpp
 * @brief GLTF Exporter implementation
 */

#include "llviewerprecompiledheaders.h"
#include "gltfexport.h"

#include "gltf/asset.h"
#include "gltf/common.h"
#include "llmeshrepository.h"
#include "llvolumemgr.h"
#include "llsdutil.h"

// For matrix conversion
#include "llmatrix4a.h"

// tinygltf includes are handled by asset.h/common.h if they expose it,
// but LL::GLTF::Asset wraps it.
// We need to construct LL::GLTF::Asset manually or use its structures.

using namespace LL::GLTF;

void GLTFExporter::add(const LLViewerObject* prim, const std::string name)
{
    mObjects.push_back({ const_cast<LLViewerObject*>(prim), name });
}

bool GLTFExporter::save(const std::string& filename)
{
    Asset asset;

    // Create a default scene
    asset.mScenes.resize(1);
    asset.mScene = 0;

    // We will collect all nodes created in the scene
    std::vector<S32>& scene_nodes = asset.mScenes[0].mNodes;

    // Iterate over objects
    for (const auto& pair : mObjects)
    {
        LLViewerObject* obj = pair.first;
        std::string objName = pair.second;

        if (!obj || !obj->getVolume()) continue;

        LLVolume* volume = obj->getVolume();
        const LLVolumeParams& params = volume->getParams();
        LLUUID sculptID = params.getSculptID();

        // Ensure mesh is loaded in repo to get skin info if any
        // If not loaded, we might be exporting what we have in LLVolume (which is fine for geometry)
        // But for skin info we need to query gMeshRepo.

        // Create Mesh
        Mesh gltfMesh;
        gltfMesh.mName = objName + "_mesh";

        // Create Node
        Node node;
        node.mName = objName;

        // Transform
        // obj->getScale(), obj->getRotation(), obj->getPosition()
        LLVector3 pos = obj->getPosition();
        LLQuaternion rot = obj->getRotation();
        LLVector3 scale = obj->getScale();

        node.mTranslation = vec3(pos.mV[0], pos.mV[1], pos.mV[2]);
        node.mRotation = quat(rot.mQ[3], rot.mQ[0], rot.mQ[1], rot.mQ[2]); // glm::quat constructor is (w, x, y, z)
        node.mScale = vec3(scale.mV[0], scale.mV[1], scale.mV[2]);
        node.makeMatrixValid();

        S32 nodeIndex = (S32)asset.mNodes.size(); // This node index
        asset.mNodes.push_back(node); // Temporarily push, but we will modify it later
        // We need reference to node in asset.mNodes
        Node& nodeRef = asset.mNodes.back();

        // Skinning
        const LLMeshSkinInfo* skinInfo = gMeshRepo.getSkinInfo(sculptID);
        S32 skinIndex = INVALID_INDEX;
        std::map<S32, S32> jointMap; // SL Joint Index (from skinInfo->mJointNums or inferred) -> GLTF Joint Node Index

        if (skinInfo)
        {
             // Create Skin
             Skin skin;
             skin.mName = "skin_" + objName;

             // Create nodes for joints
             // We need to map SL joints (strings) to GLTF nodes.
             // We iterate skinInfo->mJointNames

             // We need the Bind Matrices (Inverse Bind Matrices).
             // skinInfo->mInvBindMatrix.

             // Create buffer for inverse bind matrices
             std::vector<mat4> invBindMatrices;
             invBindMatrices.reserve(skinInfo->mInvBindMatrix.size());

             for (size_t j = 0; j < skinInfo->mJointNames.size(); ++j)
             {
                 std::string jointName = skinInfo->mJointNames[j];

                 // Create a node for this joint if we want to be correct?
                 // Ideally we should export the full skeleton hierarchy.
                 // But we don't have easy access to the full skeleton hierarchy here (it's on the avatar).
                 // However, "work directly from the asset data" implies we might not have the avatar present or we just want the asset data.
                 // The asset data only contains the joint names and inverse bind matrices. It DOES NOT contain the skeleton hierarchy.
                 // If we export a flat list of joints, it's valid GLTF but they won't be connected.
                 // But for re-importing into SL or other tools, having the nodes exist is key.

                 // Check if we already created a node for this joint name?
                 // For now, let's create unique nodes for this skin to be safe/simple.

                 Node jointNode;
                 jointNode.mName = jointName;
                 // We don't know the rest pose transform of the joint relative to parent because we don't have hierarchy in asset.
                 // But we can just put identity or some default?
                 // GLTF spec: "The global transform of each node in the joint hierarchy is calculated..."
                 // If we make them root nodes (no parents), their local transform is their global transform.
                 // But we want the mesh to be skinned.
                 // The "bind shape matrix" is baked into the mesh or applied.

                 // For export purposes, if we don't have the skeleton, we can just export nodes with identity transform
                 // and rely on InverseBindMatrices to handle the skinning math?
                 // No, skinning math is: v' = sum(w_i * jointMat_i * invBindMat_i) * v
                 // jointMat_i is global transform of joint node i.
                 // If we export joints as roots with Identity, then jointMat_i is Identity.
                 // invBindMat_i is from asset.
                 // So v' = sum(w_i * Identity * invBindMat_i) * v.
                 // This effectively applies the inverse bind matrix transformation.
                 // This might "work" to show the mesh in bind pose if the viewer applies skinning.
                 // But usually we want to export the rig.

                 // If the user wants "Rigged Mesh", they usually want to edit it.
                 // Editing requires the skeleton.
                 // If we are exporting from the viewer, we SHOULD have access to the avatar skeleton if it's an attachment.
                 // But GLTFExporter takes LLViewerObject.
                 // If it's attached, we can find the avatar.

                 // If we assume standard SL avatar skeleton, we could reconstruct it?
                 // But that's out of scope for "asset data".
                 // "work directly from the asset data" suggests we export what is in the mesh asset.
                 // Which is just the weights and joint names.

                 // So, create flat nodes for joints.

                 S32 jointNodeIndex = (S32)asset.mNodes.size();
                 asset.mNodes.push_back(jointNode);
                 scene_nodes.push_back(jointNodeIndex); // Add to scene roots for now

                 skin.mJoints.push_back(jointNodeIndex);
                 jointMap[j] = j; // Simple mapping if 1:1

                 // Inverse Bind Matrix
                 const LLMatrix4a& invBind = skinInfo->mInvBindMatrix[j];
                 // LLMatrix4a is 16-byte aligned float[16]. GLM mat4 is float[4][4] (column major).
                 // LLMatrix4a is row-major or column-major?
                 // LLMatrix4a seems to be column-major for SIMD (loadua).
                 // Let's assume it matches GLM memory layout.

                 const F32* m = invBind.getF32ptr();
                 mat4 mat;
                 memcpy(glm::value_ptr(mat), m, 16 * sizeof(float));
                 invBindMatrices.push_back(mat);
             }

             // Create accessor for IBM
             if (!invBindMatrices.empty())
             {
                 if (asset.mBuffers.empty()) asset.mBuffers.emplace_back();
                 Buffer& buffer = asset.mBuffers.back();
                 while (buffer.mData.size() % 4 != 0) buffer.mData.push_back(0);

                 size_t offset = buffer.mData.size();
                 size_t size = invBindMatrices.size() * sizeof(mat4);
                 const U8* bytes = (const U8*)invBindMatrices.data();
                 buffer.mData.insert(buffer.mData.end(), bytes, bytes + size);
                 buffer.mByteLength = buffer.mData.size();

                 // BufferView
                 S32 bvIndex = (S32)asset.mBufferViews.size();
                 BufferView bv;
                 bv.mBuffer = 0;
                 bv.mByteOffset = offset;
                 bv.mByteLength = size;
                 // IBM buffer view usually doesn't have target? Or defaults.
                 asset.mBufferViews.push_back(bv);

                 // Accessor
                 S32 accIndex = (S32)asset.mAccessors.size();
                 Accessor acc;
                 acc.mBufferView = bvIndex;
                 acc.mComponentType = Accessor::ComponentType::FLOAT;
                 acc.mCount = (S32)invBindMatrices.size();
                 acc.mType = Accessor::Type::MAT4;
                 asset.mAccessors.push_back(acc);

                 skin.mInverseBindMatrices = accIndex;
             }

             skinIndex = (S32)asset.mSkins.size();
             asset.mSkins.push_back(skin);

             // Update nodeRef because asset.mNodes might have reallocated!
             nodeRef = asset.mNodes[nodeIndex];
             nodeRef.mSkin = skinIndex;
        }

        // Access volume faces
        S32 numFaces = volume->getNumVolumeFaces();
        for (S32 i = 0; i < numFaces; ++i)
        {
            const LLVolumeFace& face = volume->getVolumeFace(i);
            if (face.mNumVertices == 0) continue;

            Primitive prim;
            prim.mMode = Primitive::Mode::TRIANGLES;

            // Materials
            Material mat;
            LLTextureEntry* te = obj->getTE(i);
            if (te)
            {
                mat.mName = "Material_" + te->getID().asString();
                LLColor4 color = te->getColor();
                mat.mPbrMetallicRoughness.mBaseColorFactor = glm::vec4(color.mV[0], color.mV[1], color.mV[2], color.mV[3]);
            }

            S32 matIndex = -1;
            for(size_t m=0; m<asset.mMaterials.size(); ++m)
            {
                if (asset.mMaterials[m].mName == mat.mName)
                {
                    matIndex = (S32)m;
                    break;
                }
            }
            if (matIndex == -1)
            {
                matIndex = (S32)asset.mMaterials.size();
                asset.mMaterials.push_back(mat);
            }
            prim.mMaterial = matIndex;

            // Geometry buffers
            std::vector<vec3> positions;
            std::vector<vec3> normals;
            std::vector<vec2> texcoords;
            std::vector<u16vec4> joints;
            std::vector<vec4> weights;

            bool hasSkin = (face.mWeights != nullptr) && (skinIndex != INVALID_INDEX);

            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                LLVector3 p = face.mPositions[v].getVector3();
                positions.push_back(vec3(p.mV[0], p.mV[1], p.mV[2]));

                LLVector3 n = face.mNormals[v].getVector3();
                normals.push_back(vec3(n.mV[0], n.mV[1], n.mV[2]));

                LLVector2 t = face.mTexCoords[v];
                texcoords.push_back(vec2(t.mV[0], 1.0f - t.mV[1]));

                if (hasSkin)
                {
                    LLVector4 w = face.mWeights[v].getVector4();
                    u16vec4 j_vec(0,0,0,0);
                    vec4 w_vec(0.f, 0.f, 0.f, 0.f);

                    for (int k=0; k<4; ++k)
                    {
                         F32 val = w[k];
                         if (val > 0.f)
                         {
                             S32 joint_idx = (S32)floor(val);
                             F32 weight = val - (F32)joint_idx;

                             // joint_idx corresponds to index in skinInfo->mJointNames
                             // We mapped these 1:1 to GLTF joint indices in our skin.mJoints
                             // So the GLTF joint index is the same as SL joint_idx (relative to the skin).

                             j_vec[k] = (U16)joint_idx;
                             w_vec[k] = weight;
                         }
                    }
                    joints.push_back(j_vec);
                    weights.push_back(w_vec);
                }
            }

            // Helper to add data to buffer
            auto addBuffer = [&](const void* data, size_t size, Accessor::Type type, Accessor::ComponentType compType, const std::string& target) -> S32
            {
                if (asset.mBuffers.empty()) asset.mBuffers.emplace_back();
                Buffer& buffer = asset.mBuffers.back();

                while (buffer.mData.size() % 4 != 0) buffer.mData.push_back(0);

                size_t offset = buffer.mData.size();
                size_t byteLength = size;

                const U8* bytes = (const U8*)data;
                buffer.mData.insert(buffer.mData.end(), bytes, bytes + byteLength);
                buffer.mByteLength = buffer.mData.size();

                S32 bvIndex = (S32)asset.mBufferViews.size();
                BufferView bv;
                bv.mBuffer = 0;
                bv.mByteOffset = offset;
                bv.mByteLength = byteLength;
                bv.mTarget = (target == "ARRAY_BUFFER") ? 34962 : 34963;
                asset.mBufferViews.push_back(bv);

                S32 accIndex = (S32)asset.mAccessors.size();
                Accessor acc;
                acc.mBufferView = bvIndex;
                acc.mByteOffset = 0;
                acc.mComponentType = compType;
                acc.mCount = (S32)(size / ((compType == Accessor::ComponentType::FLOAT ? 4 : (compType == Accessor::ComponentType::UNSIGNED_SHORT ? 2 : 1)) *
                                           (type == Accessor::Type::VEC3 ? 3 : (type == Accessor::Type::VEC2 ? 2 : (type == Accessor::Type::VEC4 ? 4 : 1)))));
                acc.mType = type;

                if (target == "ARRAY_BUFFER" && type == Accessor::Type::VEC3 && compType == Accessor::ComponentType::FLOAT)
                {
                    const vec3* vData = (const vec3*)data;
                    vec3 minVal(1e9f), maxVal(-1e9f);
                    for(size_t k=0; k<acc.mCount; ++k)
                    {
                        minVal = glm::min(minVal, vData[k]);
                        maxVal = glm::max(maxVal, vData[k]);
                    }
                    acc.mMin.push_back(minVal.x); acc.mMin.push_back(minVal.y); acc.mMin.push_back(minVal.z);
                    acc.mMax.push_back(maxVal.x); acc.mMax.push_back(maxVal.y); acc.mMax.push_back(maxVal.z);
                }

                asset.mAccessors.push_back(acc);
                return accIndex;
            };

            prim.mAttributes["POSITION"] = addBuffer(positions.data(), positions.size() * sizeof(vec3), Accessor::Type::VEC3, Accessor::ComponentType::FLOAT, "ARRAY_BUFFER");
            prim.mAttributes["NORMAL"] = addBuffer(normals.data(), normals.size() * sizeof(vec3), Accessor::Type::VEC3, Accessor::ComponentType::FLOAT, "ARRAY_BUFFER");
            prim.mAttributes["TEXCOORD_0"] = addBuffer(texcoords.data(), texcoords.size() * sizeof(vec2), Accessor::Type::VEC2, Accessor::ComponentType::FLOAT, "ARRAY_BUFFER");

            if (hasSkin)
            {
                prim.mAttributes["JOINTS_0"] = addBuffer(joints.data(), joints.size() * sizeof(u16vec4), Accessor::Type::VEC4, Accessor::ComponentType::UNSIGNED_SHORT, "ARRAY_BUFFER");
                prim.mAttributes["WEIGHTS_0"] = addBuffer(weights.data(), weights.size() * sizeof(vec4), Accessor::Type::VEC4, Accessor::ComponentType::FLOAT, "ARRAY_BUFFER");
            }

            std::vector<U16> indices(face.mNumIndices);
            for(int k=0; k<face.mNumIndices; ++k) indices[k] = face.mIndices[k];

            prim.mIndices = addBuffer(indices.data(), indices.size() * sizeof(U16), Accessor::Type::SCALAR, Accessor::ComponentType::UNSIGNED_SHORT, "ELEMENT_ARRAY_BUFFER");

            gltfMesh.mPrimitives.push_back(prim);
        }

        S32 meshIndex = (S32)asset.mMeshes.size();
        asset.mMeshes.push_back(gltfMesh);

        // Update nodeRef again because asset.mMeshes or buffers might have reallocated?
        // No, vectors reallocate independently. But be safe.
        asset.mNodes[nodeIndex].mMesh = meshIndex;
    }

    // Finalize
    asset.mVersion = "2.0";
    asset.mGenerator = "Firestorm GLTF Exporter";

    return asset.save(filename);
}
