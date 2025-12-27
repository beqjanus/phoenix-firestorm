/**
 * @file vjlocalmeshimportgltf.cpp
 * @author Vaalith Jinn
 * @brief Local Mesh GLTF importer source
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Local Mesh contribution source code
 * Copyright (C) 2022, Vaalith Jinn.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * $/LicenseInfo$
 */

 /* precompiled headers */
#include "llviewerprecompiledheaders.h"

/* own header */
#include "vjlocalmeshimportgltf.h"

/* linden headers */
#include "llviewercontrol.h" // for gSavedSettings
#include "llvoavatarself.h"
#include "llerror.h"
#include "gltf/llgltfloader.h"

// For dummy callbacks
#include "llmodelloader.h"

LLLocalMeshImportGLTF::loadFile_return LLLocalMeshImportGLTF::loadFile(LLLocalMeshFile* data, LLLocalMeshFileLOD lod)
{
    pushLog("GLTF Importer", "Starting");
    LL_DEBUGS("LocalMesh") << "GLTF Importer: Starting" << LL_ENDL;

    mLoadingLog.clear();
    std::string filename = data->getFilename(lod);

    // Prepare arguments for LLGLTFLoader
    // We need to provide dummy or functional callbacks/maps
    JointTransformMap jointTransformMap;
    JointNameSet jointsFromNodes;
    std::map<std::string, std::string, std::less<>> jointAliasMap;
    std::vector<LLJointData> viewer_skeleton;

    // Populate jointAliasMap from avatar
    JointMap avatarJoints = gAgentAvatarp->getJointAliases();
    for (const auto& alias : avatarJoints)
    {
        jointAliasMap[alias.first] = alias.second;
    }

    // Dummy callbacks
    auto load_cb = [](LLModelLoader* loader, LLModelLoader::EStatus status, void* userdata) {};
    auto joint_lookup_func = [](const std::string& name, void* userdata) -> LLJoint* { return nullptr; };
    auto texture_load_func = [](const std::string& name, void* userdata) {};
    auto state_cb = [](LLModelLoader* loader, LLModelLoader::EStatus status, void* userdata) {};

    // Max joints per mesh usually 110 for SL
    U32 maxJointsPerMesh = 110;
    U32 modelLimit = 256; // Reasonable limit?
    U32 debugMode = 0;

    // Workaround: We can't easily access mModelList if it's protected.
    // However, LLModelLoader has getModelList()? No.
    // Let's check LLModelLoader definition.
    // It seems LLModelLoader doesn't expose mModelList publicly.

    // BUT, we can make a small subclass to expose it, or check if there is a public getter.
    // Since I can't modify LLModelLoader easily (it's core), I will define a helper struct that inherits LLGLTFLoader
    // and exposes the list.

    struct ExposedGLTFLoader : public LLGLTFLoader
    {
        using LLGLTFLoader::LLGLTFLoader; // inherit constructors
        std::vector<LLModel*>& getModels() { return mModelList; }
    };

    ExposedGLTFLoader exposedLoader(
        filename,
        (S32)lod,
        load_cb,
        joint_lookup_func,
        texture_load_func,
        state_cb,
        nullptr,
        jointTransformMap,
        jointsFromNodes,
        jointAliasMap,
        maxJointsPerMesh,
        modelLimit,
        debugMode,
        viewer_skeleton
    );

    if (!exposedLoader.OpenFile(filename))
    {
        pushLog("GLTF Importer", "Failed to open/parse GLTF file.");
        return loadFile_return(false, mLoadingLog);
    }

    std::vector<LLModel*>& models = exposedLoader.getModels();

    if (models.empty())
    {
        pushLog("GLTF Importer", "No models found in GLTF file.");
        return loadFile_return(false, mLoadingLog);
    }

    pushLog("GLTF Importer", "Found " + std::to_string(models.size()) + " models.");

    auto& object_vector = data->getObjectVector();

    LLMatrix4 scene_transform_base;
    scene_transform_base.setIdentity();

    // Iterate models and convert to LLLocalMeshObject
    for (size_t i = 0; i < models.size(); ++i)
    {
        LLModel* model = models[i];
        std::string object_name = model->mLabel;
        if (object_name.empty()) object_name = "object_" + std::to_string(i);

        pushLog("GLTF Importer", "Processing object: " + object_name);

        LLLocalMeshObject* current_object = nullptr;

        if (lod == LLLocalMeshFileLOD::LOCAL_LOD_HIGH)
        {
            auto new_object = std::make_unique<LLLocalMeshObject>(object_name);
            current_object = new_object.get();
            object_vector.push_back(std::move(new_object));
        }
        else
        {
            // For lower LODs, try to match by index? Or name?
            // LLLocalMeshImportDAE matches by index basically (iterating meshes in order).
            // Here we iterate models in order.
            if (i < object_vector.size())
            {
                current_object = object_vector[i].get();
            }
            else
            {
                pushLog("GLTF Importer", "LOD " + std::to_string(lod) + " has more objects than LOD High, skipping extra object " + object_name);
                continue;
            }
        }

        // Convert LLModel faces to LLLocalMeshFaces
        auto& faces = current_object->getFaces(lod);
        faces.clear();

        for (S32 face_idx = 0; face_idx < model->getNumVolumeFaces(); ++face_idx)
        {
            const LLVolumeFace& volFace = model->getVolumeFace(face_idx);
            auto localFace = std::make_unique<LLLocalMeshFace>();

            // Copy indices
            auto& indices = localFace->getIndices();
            indices.reserve(volFace.mNumIndices);
            for (S32 j = 0; j < volFace.mNumIndices; ++j)
            {
                indices.push_back(volFace.mIndices[j]);
            }

            // Copy vertices
            auto& positions = localFace->getPositions();
            auto& normals = localFace->getNormals();
            auto& uvs = localFace->getUVs();
            auto& skin = localFace->getSkin();

            positions.resize(volFace.mNumVertices);
            normals.resize(volFace.mNumVertices);
            uvs.resize(volFace.mNumVertices);

            // Bounding box calculation
            LLVector4 min_bbox(1e9f, 1e9f, 1e9f, 0.f);
            LLVector4 max_bbox(-1e9f, -1e9f, -1e9f, 0.f);

            for (S32 v = 0; v < volFace.mNumVertices; ++v)
            {
                LLVector3 pos = volFace.mPositions[v].getVector3();
                positions[v] = LLVector4(pos.mV[0], pos.mV[1], pos.mV[2], 1.f);

                LLVector3 norm = volFace.mNormals[v].getVector3();
                normals[v] = LLVector4(norm.mV[0], norm.mV[1], norm.mV[2], 0.f);

                uvs[v] = volFace.mTexCoords[v];

                // Update bbox
                for(int k=0; k<3; ++k) {
                    if (pos.mV[k] < min_bbox.mV[k]) min_bbox.mV[k] = pos.mV[k];
                    if (pos.mV[k] > max_bbox.mV[k]) max_bbox.mV[k] = pos.mV[k];
                }
            }

            localFace->setFaceBoundingBox(min_bbox, true); // set min as initial
            localFace->setFaceBoundingBox(max_bbox, false); // update with max

            // Copy skin weights if present
            if (volFace.mWeights)
            {
                skin.resize(volFace.mNumVertices);
                for (S32 v = 0; v < volFace.mNumVertices; ++v)
                {
                    LLLocalMeshFace::LLLocalMeshSkinUnit& unit = skin[v];
                    // Init to -1 and 0
                    for(int k=0; k<4; ++k) {
                        unit.mJointIndices[k] = -1;
                        unit.mJointWeights[k] = 0.f;
                    }

                    LLVector4 weights = volFace.mWeights[v].getVector4();
                    // In LLVolumeFace, weights are stored somewhat packed?
                    // LLVolumeFace::mWeights is LLVector4a*.
                    // Wait, LLVolumeFace::mWeights comments say:
                    // "Only used if mWeights is not NULL.  Weights are stored as 4 floats per vertex.
                    // The integer part of the float is the joint index, the fractional part is the weight."

                    for (int k = 0; k < 4; ++k)
                    {
                        F32 val = weights[k];
                        if (val > 0.f) // Assuming valid weight entry > 0
                        {
                            S32 joint_idx = (S32)floor(val);
                            F32 weight = val - (F32)joint_idx;
                            unit.mJointIndices[k] = joint_idx;
                            unit.mJointWeights[k] = weight;
                        }
                    }
                }
            }

            faces.push_back(std::move(localFace));
        }

        // Copy Skin Info
        // LLModel has mSkinInfo
        if (model->mSkinInfo.mBindShapeMatrix.isFinite3()) // Check if valid?
        {
            // If there are joints, copy skin info
            if (!model->mSkinInfo.mJointNames.empty())
            {
                 LLPointer<LLMeshSkinInfo> skinInfo = new LLMeshSkinInfo();
                 skinInfo->mJointNames = model->mSkinInfo.mJointNames;
                 skinInfo->mJointNums = model->mSkinInfo.mJointNums;
                 skinInfo->mInvBindMatrix = model->mSkinInfo.mInvBindMatrix;
                 skinInfo->mAlternateBindMatrix = model->mSkinInfo.mAlternateBindMatrix;
                 skinInfo->mBindShapeMatrix = model->mSkinInfo.mBindShapeMatrix;
                 skinInfo->mBindPoseMatrix = model->mSkinInfo.mBindPoseMatrix;
                 skinInfo->mPelvisOffset = model->mSkinInfo.mPelvisOffset;
                 skinInfo->mLockScaleIfJointPosition = model->mSkinInfo.mLockScaleIfJointPosition;
                 skinInfo->updateHash();

                 current_object->setObjectMeshSkinInfo(skinInfo);
            }
        }

        // Post processing for object
        if (lod == LLLocalMeshFileLOD::LOCAL_LOD_HIGH)
        {
             current_object->computeObjectBoundingBox();
             current_object->computeObjectTransform(scene_transform_base);
             current_object->normalizeFaceValues(lod);
        }
        else
        {
             current_object->normalizeFaceValues(lod);
        }
    }

    pushLog("GLTF Importer", "Import complete.");
    return loadFile_return(true, mLoadingLog);
}

void LLLocalMeshImportGLTF::pushLog(const std::string& who, const std::string& what, bool is_error)
{
    std::string log_msg = "[ " + who + " ] ";
    if (is_error)
    {
        log_msg += "[ ERROR ] ";
    }

    log_msg += what;
    mLoadingLog.push_back(log_msg);
    LL_INFOS("LocalMesh") << log_msg << LL_ENDL;
}
