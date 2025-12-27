/**
 * @file gltfexport.h
 * @brief GLTF Exporter header
 */

#ifndef GLTFEXPORT_H_
#define GLTFEXPORT_H_

#include "llviewerobject.h"
#include <vector>
#include <string>

class LLViewerObject;

class GLTFExporter
{
public:
    typedef std::vector<std::pair<LLViewerObject*, std::string>> obj_info_t;

    GLTFExporter() = default;

    // Add object to be exported
    void add(const LLViewerObject* prim, const std::string name);

    // Save to file
    bool save(const std::string& filename);

    // Setters
    void setObjects(const obj_info_t& objects) { mObjects = objects; }

private:
    obj_info_t mObjects;
};

#endif
