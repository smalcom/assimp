/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2025, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file Implementation of the MDL importer class */

#ifndef ASSIMP_BUILD_NO_HMP_IMPORTER

// internal headers
#include "HMPLoader.h"
#include "AssetLib/MD2/MD2FileData.h"

#include <assimp/StringUtils.h>
#include <assimp/importerdesc.h>
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/IOSystem.hpp>

#include <memory>

using namespace Assimp;

static constexpr aiImporterDesc desc = {
    "3D GameStudio Heightmap (HMP) Importer",
    "",
    "",
    "",
    aiImporterFlags_SupportBinaryFlavour,
    0,
    0,
    0,
    0,
    "hmp"
};

// ------------------------------------------------------------------------------------------------
// Constructor to be privately used by Importer
HMPImporter::HMPImporter() = default;

// ------------------------------------------------------------------------------------------------
// Destructor, private as well
HMPImporter::~HMPImporter() = default;

// ------------------------------------------------------------------------------------------------
// Returns whether the class can handle the format of the given file.
bool HMPImporter::CanRead(const std::string &pFile, IOSystem *pIOHandler, bool /*checkSig*/) const {
    static constexpr uint32_t tokens[] = {
        AI_HMP_MAGIC_NUMBER_LE_4,
        AI_HMP_MAGIC_NUMBER_LE_5,
        AI_HMP_MAGIC_NUMBER_LE_7
    };
    return CheckMagicToken(pIOHandler, pFile, tokens, AI_COUNT_OF(tokens));
}

// ------------------------------------------------------------------------------------------------
// Get list of all file extensions that are handled by this loader
const aiImporterDesc *HMPImporter::GetInfo() const {
    return &desc;
}

// ------------------------------------------------------------------------------------------------
// Imports the given file into the given scene structure.
void HMPImporter::InternReadFile(const std::string &pFile,
        aiScene *_pScene, IOSystem *_pIOHandler) {
    pScene = _pScene;
    mIOHandler = _pIOHandler;
    std::unique_ptr<IOStream> file(mIOHandler->Open(pFile));

    // Check whether we can read from the file
    if (file == nullptr) {
        throw DeadlyImportError("Failed to open HMP file ", pFile, ".");
    }

    // Check whether the HMP file is large enough to contain
    // at least the file header
    const size_t fileSize = file->FileSize();
    if (fileSize < 50)
        throw DeadlyImportError("HMP File is too small.");

    // Allocate storage and copy the contents of the file to a memory buffer
    auto deleter=[this](uint8_t* ptr){ delete[] ptr; mBuffer = nullptr; };
    std::unique_ptr<uint8_t[], decltype(deleter)> buffer(new uint8_t[fileSize], deleter);
    mBuffer = buffer.get();
    file->Read((void *)mBuffer, 1, fileSize);
    iFileSize = (unsigned int)fileSize;

    // Determine the file subtype and call the appropriate member function
    const uint32_t iMagic = *((uint32_t *)this->mBuffer);

    // HMP4 format
    if (AI_HMP_MAGIC_NUMBER_LE_4 == iMagic ||
            AI_HMP_MAGIC_NUMBER_BE_4 == iMagic) {
        ASSIMP_LOG_DEBUG("HMP subtype: 3D GameStudio A4, magic word is HMP4");
        InternReadFile_HMP4();
    }
    // HMP5 format
    else if (AI_HMP_MAGIC_NUMBER_LE_5 == iMagic ||
             AI_HMP_MAGIC_NUMBER_BE_5 == iMagic) {
        ASSIMP_LOG_DEBUG("HMP subtype: 3D GameStudio A5, magic word is HMP5");
        InternReadFile_HMP5();
    }
    // HMP7 format
    else if (AI_HMP_MAGIC_NUMBER_LE_7 == iMagic ||
             AI_HMP_MAGIC_NUMBER_BE_7 == iMagic) {
        ASSIMP_LOG_DEBUG("HMP subtype: 3D GameStudio A7, magic word is HMP7");
        InternReadFile_HMP7();
    } else {
        // Print the magic word to the logger
        std::string szBuffer = ai_str_toprintable((const char *)&iMagic, sizeof(iMagic));

        // We're definitely unable to load this file
        throw DeadlyImportError("Unknown HMP subformat ", pFile,
                                ". Magic word (", szBuffer, ") is not known");
    }

    // Set the AI_SCENE_FLAGS_TERRAIN bit
    pScene->mFlags |= AI_SCENE_FLAGS_TERRAIN;
}

// ------------------------------------------------------------------------------------------------
void HMPImporter::ValidateHeader_HMP457() {
    const HMP::Header_HMP5 *const pcHeader = (const HMP::Header_HMP5 *)mBuffer;

    if (120 > iFileSize) {
        throw DeadlyImportError("HMP file is too small (header size is "
                                "120 bytes, this file is smaller)");
    }

    if (!std::isfinite(pcHeader->ftrisize_x) || !std::isfinite(pcHeader->ftrisize_y))
        throw DeadlyImportError("Size of triangles in either x or y direction is not finite");

    if (!pcHeader->ftrisize_x || !pcHeader->ftrisize_y)
        throw DeadlyImportError("Size of triangles in either x or y direction is zero");

    if (!std::isfinite(pcHeader->fnumverts_x))
        throw DeadlyImportError("Number of triangles in x direction is not finite");

    if (pcHeader->fnumverts_x < 1.0f || (pcHeader->numverts / pcHeader->fnumverts_x) < 1.0f)
        throw DeadlyImportError("Number of triangles in either x or y direction is zero");

    if (!pcHeader->numframes)
        throw DeadlyImportError("There are no frames. At least one should be there");
}

// ------------------------------------------------------------------------------------------------
void HMPImporter::InternReadFile_HMP4() {
    throw DeadlyImportError("HMP4 is currently not supported");
}

// ------------------------------------------------------------------------------------------------
void HMPImporter::InternReadFile_HMP5() {
    // read the file header and skip everything to byte 84
    const HMP::Header_HMP5 *pcHeader = (const HMP::Header_HMP5 *)mBuffer;
    const unsigned char *szCurrent = (const unsigned char *)(mBuffer + 84);
    ValidateHeader_HMP457();

    // generate an output mesh
    pScene->mNumMeshes = 1;
    pScene->mMeshes = new aiMesh *[1];
    aiMesh *pcMesh = pScene->mMeshes[0] = new aiMesh();

    pcMesh->mMaterialIndex = 0;
    pcMesh->mVertices = new aiVector3D[pcHeader->numverts];
    pcMesh->mNormals = new aiVector3D[pcHeader->numverts];

    const unsigned int height = (unsigned int)(pcHeader->numverts / pcHeader->fnumverts_x);
    const unsigned int width = (unsigned int)pcHeader->fnumverts_x;

    // generate/load a material for the terrain
    CreateMaterial(szCurrent, &szCurrent);

    // goto offset 120, I don't know why ...
    // (fixme) is this the frame header? I assume yes since it starts with 2.
    szCurrent += 36;
    SizeCheck(szCurrent + sizeof(const HMP::Vertex_HMP7) * height * width);

    // now load all vertices from the file
    aiVector3D *pcVertOut = pcMesh->mVertices;
    aiVector3D *pcNorOut = pcMesh->mNormals;
    const HMP::Vertex_HMP5 *src = (const HMP::Vertex_HMP5 *)szCurrent;
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            pcVertOut->x = x * pcHeader->ftrisize_x;
            pcVertOut->y = y * pcHeader->ftrisize_y;
            pcVertOut->z = (((float)src->z / 0xffff) - 0.5f) * pcHeader->ftrisize_x * 8.0f;
            MD2::LookupNormalIndex(src->normals162index, *pcNorOut);
            ++pcVertOut;
            ++pcNorOut;
            ++src;
        }
    }

    // generate texture coordinates if necessary
    if (pcHeader->numskins)
        GenerateTextureCoords(width, height);

    // now build a list of faces
    CreateOutputFaceList(width, height);

    // there is no nodegraph in HMP files. Simply assign the one mesh
    // (no, not the one ring) to the root node
    pScene->mRootNode = new aiNode();
    pScene->mRootNode->mName.Set("terrain_root");
    pScene->mRootNode->mNumMeshes = 1;
    pScene->mRootNode->mMeshes = new unsigned int[1];
    pScene->mRootNode->mMeshes[0] = 0;
}

// ------------------------------------------------------------------------------------------------
void HMPImporter::InternReadFile_HMP7() {
    // read the file header and skip everything to byte 84
    const HMP::Header_HMP5 *const pcHeader = (const HMP::Header_HMP5 *)mBuffer;
    const unsigned char *szCurrent = (const unsigned char *)(mBuffer + 84);
    ValidateHeader_HMP457();

    // generate an output mesh
    pScene->mNumMeshes = 1;
    pScene->mMeshes = new aiMesh *[1];
    aiMesh *pcMesh = pScene->mMeshes[0] = new aiMesh();

    pcMesh->mMaterialIndex = 0;
    pcMesh->mVertices = new aiVector3D[pcHeader->numverts];
    pcMesh->mNormals = new aiVector3D[pcHeader->numverts];

    const unsigned int height = (unsigned int)(pcHeader->numverts / pcHeader->fnumverts_x);
    const unsigned int width = (unsigned int)pcHeader->fnumverts_x;

    // generate/load a material for the terrain
    CreateMaterial(szCurrent, &szCurrent);

    // goto offset 120, I don't know why ...
    // (fixme) is this the frame header? I assume yes since it starts with 2.
    szCurrent += 36;

    SizeCheck(szCurrent + sizeof(const HMP::Vertex_HMP7) * height * width);

    // now load all vertices from the file
    aiVector3D *pcVertOut = pcMesh->mVertices;
    ai_assert(pcVertOut != nullptr);
    aiVector3D *pcNorOut = pcMesh->mNormals;
    ai_assert(pcNorOut != nullptr);
    const HMP::Vertex_HMP7 *src = (const HMP::Vertex_HMP7 *)szCurrent;
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            pcVertOut->x = x * pcHeader->ftrisize_x;
            pcVertOut->y = y * pcHeader->ftrisize_y;

            // FIXME: What exctly is the correct scaling factor to use?
            // possibly pcHeader->scale_origin[2] in combination with a
            // signed interpretation of src->z?
            pcVertOut->z = (((float)src->z / 0xffff) - 0.5f) * pcHeader->ftrisize_x * 8.0f;

            pcNorOut->x = ((float)src->normal_x / 0x80); // * pcHeader->scale_origin[0];
            pcNorOut->y = ((float)src->normal_y / 0x80); // * pcHeader->scale_origin[1];
            pcNorOut->z = 1.0f;
            pcNorOut->Normalize();

            ++pcVertOut;
            ++pcNorOut;
            ++src;
        }
    }

    // generate texture coordinates if necessary
    if (pcHeader->numskins) GenerateTextureCoords(width, height);

    // now build a list of faces
    CreateOutputFaceList(width, height);

    // there is no nodegraph in HMP files. Simply assign the one mesh
    // (no, not the One Ring) to the root node
    pScene->mRootNode = new aiNode();
    pScene->mRootNode->mName.Set("terrain_root");
    pScene->mRootNode->mNumMeshes = 1;
    pScene->mRootNode->mMeshes = new unsigned int[1];
    pScene->mRootNode->mMeshes[0] = 0;
}

// ------------------------------------------------------------------------------------------------
void HMPImporter::CreateMaterial(const unsigned char *szCurrent,
        const unsigned char **szCurrentOut) {
    aiMesh *const pcMesh = pScene->mMeshes[0];
    const HMP::Header_HMP5 *const pcHeader = (const HMP::Header_HMP5 *)mBuffer;

    // we don't need to generate texture coordinates if
    // we have no textures in the file ...
    if (pcHeader->numskins) {
        pcMesh->mTextureCoords[0] = new aiVector3D[pcHeader->numverts];
        pcMesh->mNumUVComponents[0] = 2;

        // now read the first skin and skip all others
        ReadFirstSkin(pcHeader->numskins, szCurrent, &szCurrent);
        *szCurrentOut = szCurrent;
        return;
    }

    // generate a default material
    const int iMode = (int)aiShadingMode_Gouraud;
    aiMaterial *pcHelper = new aiMaterial();
    pcHelper->AddProperty<int>(&iMode, 1, AI_MATKEY_SHADING_MODEL);

    aiColor3D clr;
    clr.b = clr.g = clr.r = 0.6f;
    pcHelper->AddProperty<aiColor3D>(&clr, 1, AI_MATKEY_COLOR_DIFFUSE);
    pcHelper->AddProperty<aiColor3D>(&clr, 1, AI_MATKEY_COLOR_SPECULAR);

    clr.b = clr.g = clr.r = 0.05f;
    pcHelper->AddProperty<aiColor3D>(&clr, 1, AI_MATKEY_COLOR_AMBIENT);

    aiString szName;
    szName.Set(AI_DEFAULT_MATERIAL_NAME);
    pcHelper->AddProperty(&szName, AI_MATKEY_NAME);

    // add the material to the scene
    pScene->mNumMaterials = 1;
    pScene->mMaterials = new aiMaterial *[1];
    pScene->mMaterials[0] = pcHelper;
    *szCurrentOut = szCurrent;
}

// ------------------------------------------------------------------------------------------------
void HMPImporter::CreateOutputFaceList(unsigned int width, unsigned int height) {
    aiMesh *const pcMesh = this->pScene->mMeshes[0];

    // Allocate enough storage
    pcMesh->mNumFaces = (width - 1) * (height - 1);
    pcMesh->mFaces = new aiFace[pcMesh->mNumFaces];

    pcMesh->mNumVertices = pcMesh->mNumFaces * 4;
    aiVector3D *pcVertices = new aiVector3D[pcMesh->mNumVertices];
    aiVector3D *pcNormals = new aiVector3D[pcMesh->mNumVertices];

    aiFace *pcFaceOut(pcMesh->mFaces);
    aiVector3D *pcVertOut = pcVertices;
    aiVector3D *pcNorOut = pcNormals;

    aiVector3D *pcUVs = pcMesh->mTextureCoords[0] ? new aiVector3D[pcMesh->mNumVertices] : nullptr;
    aiVector3D *pcUVOut(pcUVs);

    // Build the terrain square
    const unsigned int upperBound = pcMesh->mNumVertices;
    unsigned int iCurrent = 0;
    for (unsigned int y = 0; y < height - 1; ++y) {
        const size_t offset0 = y * width;
        const size_t offset1 = (y + 1) * width;
        for (unsigned int x = 0; x < width - 1; ++x, ++pcFaceOut) {
            pcFaceOut->mNumIndices = 4;
            pcFaceOut->mIndices = new unsigned int[4];
            if ((offset0 + x + 1) >= upperBound){
                continue;
            }
            if ((offset1 + x + 1) >= upperBound){
                continue;
            }

            *pcVertOut++ = pcMesh->mVertices[offset0 + x];
            *pcVertOut++ = pcMesh->mVertices[offset1 + x];
            *pcVertOut++ = pcMesh->mVertices[offset1 + x + 1];
            *pcVertOut++ = pcMesh->mVertices[offset0 + x + 1];

            *pcNorOut++ = pcMesh->mNormals[offset0 + x];
            *pcNorOut++ = pcMesh->mNormals[offset1 + x];
            *pcNorOut++ = pcMesh->mNormals[offset1 + x + 1];
            *pcNorOut++ = pcMesh->mNormals[offset0 + x + 1];

            if (pcMesh->mTextureCoords[0]) {
                *pcUVOut++ = pcMesh->mTextureCoords[0][offset0 + x];
                *pcUVOut++ = pcMesh->mTextureCoords[0][offset1 + x];
                *pcUVOut++ = pcMesh->mTextureCoords[0][offset1 + x + 1];
                *pcUVOut++ = pcMesh->mTextureCoords[0][offset0 + x + 1];
            }

            for (unsigned int i = 0; i < 4; ++i)
                pcFaceOut->mIndices[i] = iCurrent++;
        }
    }
    delete[] pcMesh->mVertices;
    pcMesh->mVertices = pcVertices;

    delete[] pcMesh->mNormals;
    pcMesh->mNormals = pcNormals;

    if (pcMesh->mTextureCoords[0]) {
        delete[] pcMesh->mTextureCoords[0];
        pcMesh->mTextureCoords[0] = pcUVs;
    }
}

// ------------------------------------------------------------------------------------------------
void HMPImporter::ReadFirstSkin(unsigned int iNumSkins, const unsigned char *szCursor,
        const unsigned char **szCursorOut) {
    ai_assert(0 != iNumSkins);
    ai_assert(nullptr != szCursor);

    // read the type of the skin ...
    // sometimes we need to skip 12 bytes here, I don't know why ...
    uint32_t iType = *((uint32_t *)szCursor);
    szCursor += sizeof(uint32_t);
    if (0 == iType) {
        szCursor += sizeof(uint32_t) * 2;
        iType = *((uint32_t *)szCursor);
        szCursor += sizeof(uint32_t);
        if (!iType)
            throw DeadlyImportError("Unable to read HMP7 skin chunk");
    }
    // read width and height
    uint32_t iWidth = *((uint32_t *)szCursor);
    szCursor += sizeof(uint32_t);
    uint32_t iHeight = *((uint32_t *)szCursor);
    szCursor += sizeof(uint32_t);

    // allocate an output material
    std::unique_ptr<aiMaterial> pcMat(new aiMaterial());

    // read the skin, this works exactly as for MDL7
    ParseSkinLump_3DGS_MDL7(szCursor, &szCursor,
            pcMat.get(), iType, iWidth, iHeight);

    // now we need to skip any other skins ...
    for (unsigned int i = 1; i < iNumSkins; ++i) {
        SizeCheck(szCursor + 3 * sizeof(uint32_t));
        iType = *((uint32_t *)szCursor);
        szCursor += sizeof(uint32_t);
        iWidth = *((uint32_t *)szCursor);
        szCursor += sizeof(uint32_t);
        iHeight = *((uint32_t *)szCursor);
        szCursor += sizeof(uint32_t);

        SkipSkinLump_3DGS_MDL7(szCursor, &szCursor, iType, iWidth, iHeight);
        SizeCheck(szCursor);
    }

    // setup the material ...
    pScene->mNumMaterials = 1;
    pScene->mMaterials = new aiMaterial *[1];
    pScene->mMaterials[0] = pcMat.release();

    *szCursorOut = szCursor;
}

// ------------------------------------------------------------------------------------------------
// Generate proepr texture coords
void HMPImporter::GenerateTextureCoords(const unsigned int width, const unsigned int height) {
    ai_assert(nullptr != pScene->mMeshes);
    ai_assert(nullptr != pScene->mMeshes[0]);
    ai_assert(nullptr != pScene->mMeshes[0]->mTextureCoords[0]);

    aiVector3D *uv = pScene->mMeshes[0]->mTextureCoords[0];
    if (uv == nullptr) {
        return;
    }

    if (height == 0.0f || width == 0.0) {
        return;
    }

    const float fY = (1.0f / height) + (1.0f / height) / height;
    const float fX = (1.0f / width) + (1.0f / width) / width;

    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x, ++uv) {
            uv->y = fY * y;
            uv->x = fX * x;
            uv->z = 0.0f;
        }
    }
}

#endif // !! ASSIMP_BUILD_NO_HMP_IMPORTER
