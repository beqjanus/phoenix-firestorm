/**
 * @file vjlocalmeshimportgltf.h
 * @author Vaalith Jinn
 * @brief Local Mesh GLTF importer header
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

#pragma once

#include "vjlocalmesh.h"
#include <map>
#include <vector>
#include <string>

// formal declarations
class LLLocalMeshObject;
class LLLocalMeshFile;

class LLLocalMeshImportGLTF
{
public:
    typedef std::pair<bool, std::vector<std::string>> loadFile_return;
    loadFile_return loadFile(LLLocalMeshFile* data, LLLocalMeshFileLOD lod);

private:
    void pushLog(const std::string& who, const std::string& what, bool is_error = false);
    std::vector<std::string> mLoadingLog;
};
