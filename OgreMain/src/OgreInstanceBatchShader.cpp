/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2009 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreStableHeaders.h"
#include "OgreInstanceBatchShader.h"
#include "OgreSubMesh.h"
#include "OgreRenderOperation.h"
#include "OgreHardwareBufferManager.h"
#include "OgreInstancedEntity.h"
#include "OgreMaterial.h"
#include "OgreTechnique.h"

namespace Ogre
{
	InstanceBatchShader::InstanceBatchShader( MeshPtr &meshReference, const MaterialPtr &material,
										size_t instancesPerBatch, const Mesh::IndexMap *indexToBoneMap,
										const String &batchName ) :
				InstanceBatch( meshReference, material, instancesPerBatch, indexToBoneMap, batchName ),
				m_numWorldMatrices( instancesPerBatch )
	{
	}

	InstanceBatchShader::~InstanceBatchShader()
	{
	}

	//-----------------------------------------------------------------------
	size_t InstanceBatchShader::calculateMaxNumInstances( const SubMesh *baseSubMesh ) const
	{
		const size_t numBones = std::max<size_t>( 1, baseSubMesh->blendIndexToBoneIndexMap.size() );

		m_material->load();
		Technique *technique = m_material->getBestTechnique();
		if( technique )
		{
			GpuProgramParametersSharedPtr vertexParam = technique->getPass(0)->getVertexProgramParameters();
			GpuConstantDefinitionIterator itor = vertexParam->getConstantDefinitionIterator();
			while( itor.hasMoreElements() )
			{
				GpuConstantDefinition &constDef = itor.getNext();
				if( constDef.constType == GCT_MATRIX_3X4 && constDef.isFloat() )
				{
					const GpuProgramParameters::AutoConstantEntry *entry =
									vertexParam->_findRawAutoConstantEntryFloat( constDef.physicalIndex );
					if( entry->paramType == GpuProgramParameters::ACT_WORLD_MATRIX_ARRAY_3x4 )
					{
						size_t retVal = constDef.arraySize / numBones;
						if( retVal < 3 )
						{
							LogManager::getSingleton().logMessage( "InstanceBatchShader: Mesh " +
										m_meshReference->getName() + " using material " +
										m_material->getName() + " contains many bones. The ammount of "
										"instances per batch is very low. Performance benefits will "
										"be minimal, if any. It might be even slower!",
										LML_NORMAL );
						}
						return retVal;
					}
				}
			}
		}

		//Material is malformed or the technique is just unsupported.

		return 0;
	}
	//-----------------------------------------------------------------------
	void InstanceBatchShader::buildFrom( const SubMesh *baseSubMesh, const RenderOperation &renderOperation )
	{
		if( m_meshReference->hasSkeleton() && !m_meshReference->getSkeleton().isNull() )
			m_numWorldMatrices = m_instancesPerBatch * baseSubMesh->blendIndexToBoneIndexMap.size();
		InstanceBatch::buildFrom( baseSubMesh, renderOperation );
	}
	//-----------------------------------------------------------------------
	void InstanceBatchShader::setupVertices( const SubMesh* baseSubMesh )
	{
		m_renderOperation.vertexData = OGRE_NEW VertexData();

		VertexData *thisVertexData = m_renderOperation.vertexData;
		VertexData *baseVertexData = baseSubMesh->vertexData;

		thisVertexData->vertexStart = 0;
		thisVertexData->vertexCount = baseVertexData->vertexCount * m_instancesPerBatch;

		HardwareBufferManager::getSingleton().destroyVertexDeclaration( thisVertexData->vertexDeclaration );
		thisVertexData->vertexDeclaration = baseVertexData->vertexDeclaration->clone();

		if( m_meshReference->hasSkeleton() && !m_meshReference->getSkeleton().isNull() )
		{
			//Building hw skinned batches follow a different path
			setupHardwareSkinned( baseSubMesh, thisVertexData, baseVertexData );
			return;
		}

		//TODO: Can't we, instead of using another source, put the index ID in the same source?
		thisVertexData->vertexDeclaration->addElement(
										thisVertexData->vertexDeclaration->getMaxSource() + 1, 0,
										VET_UBYTE4, VES_BLEND_INDICES );


		for( size_t i=0; i<thisVertexData->vertexDeclaration->getMaxSource(); ++i )
		{
			//Create our own vertex buffer
			HardwareVertexBufferSharedPtr vertexBuffer =
											HardwareBufferManager::getSingleton().createVertexBuffer(
											thisVertexData->vertexDeclaration->getVertexSize(i),
											thisVertexData->vertexCount,
											HardwareBuffer::HBU_STATIC_WRITE_ONLY );
			thisVertexData->vertexBufferBinding->setBinding( i, vertexBuffer );

			//Grab the base submesh data
			HardwareVertexBufferSharedPtr baseVertexBuffer =
													baseVertexData->vertexBufferBinding->getBuffer(i);

			char* thisBuf = static_cast<char*>(vertexBuffer->lock(HardwareBuffer::HBL_DISCARD));
			char* baseBuf = static_cast<char*>(baseVertexBuffer->lock(HardwareBuffer::HBL_DISCARD));

			//Copy and repeat
			for( size_t j=0; j<m_instancesPerBatch; ++j )
			{
				const size_t sizeOfBuffer = baseVertexData->vertexCount *
											baseVertexData->vertexDeclaration->getVertexSize(i);
				memcpy( thisBuf + j * sizeOfBuffer, baseBuf, sizeOfBuffer );
			}

			baseVertexBuffer->unlock();
			vertexBuffer->unlock();
		}

		{
			//Now create the vertices "index ID" to individualize each instance
			const unsigned short lastSource = thisVertexData->vertexDeclaration->getMaxSource();
			HardwareVertexBufferSharedPtr vertexBuffer =
											HardwareBufferManager::getSingleton().createVertexBuffer(
											thisVertexData->vertexDeclaration->getVertexSize( lastSource ),
											thisVertexData->vertexCount,
											HardwareBuffer::HBU_STATIC_WRITE_ONLY );
			thisVertexData->vertexBufferBinding->setBinding( lastSource, vertexBuffer );

			char* thisBuf = static_cast<char*>(vertexBuffer->lock(HardwareBuffer::HBL_DISCARD));
			for( size_t j=0; j<m_instancesPerBatch; ++j )
			{
				for( size_t k=0; k<baseVertexData->vertexCount; ++k )
				{
					*thisBuf++ = j;
					*thisBuf++ = j;
					*thisBuf++ = j;
					*thisBuf++ = j;
				}
			}

			vertexBuffer->unlock();
		}
	}
	//-----------------------------------------------------------------------
	void InstanceBatchShader::setupIndices( const SubMesh* baseSubMesh )
	{
		m_renderOperation.indexData = OGRE_NEW IndexData();

		IndexData *thisIndexData = m_renderOperation.indexData;
		IndexData *baseIndexData = baseSubMesh->indexData;

		thisIndexData->indexStart = 0;
		thisIndexData->indexCount = baseIndexData->indexCount * m_instancesPerBatch;

		//TODO: Support 32-bit and check numVertices is below max supported by GPU
		thisIndexData->indexBuffer = HardwareBufferManager::getSingleton().createIndexBuffer(
										HardwareIndexBuffer::IT_16BIT,
										thisIndexData->indexCount, HardwareBuffer::HBU_STATIC_WRITE_ONLY );

		uint16 *thisBuf = static_cast<uint16*>(thisIndexData->indexBuffer->
																lock( HardwareBuffer::HBL_DISCARD ));
		uint16 *initBaseBuf = static_cast<uint16*>(baseIndexData->indexBuffer->
																lock( HardwareBuffer::HBL_DISCARD ));

		for( size_t i=0; i<m_instancesPerBatch; ++i )
		{
			uint16 const *initBuf		= initBaseBuf;
			const size_t vertexOffset	= i * m_renderOperation.vertexData->vertexCount /
											m_instancesPerBatch;

			for( size_t j=0; j<baseIndexData->indexCount; ++j )
			{
				*thisBuf++ = *initBuf + vertexOffset;
				++initBuf;
			}
		}

		baseIndexData->indexBuffer->unlock();
		thisIndexData->indexBuffer->unlock();
	}
	//-----------------------------------------------------------------------
	void InstanceBatchShader::setupHardwareSkinned( const SubMesh* baseSubMesh, VertexData *thisVertexData,
													VertexData *baseVertexData )
	{
		const size_t numBones = baseSubMesh->blendIndexToBoneIndexMap.size();
		m_numWorldMatrices = m_instancesPerBatch * numBones;

		for( size_t i=0; i<=thisVertexData->vertexDeclaration->getMaxSource(); ++i )
		{
			//Create our own vertex buffer
			HardwareVertexBufferSharedPtr vertexBuffer =
											HardwareBufferManager::getSingleton().createVertexBuffer(
											thisVertexData->vertexDeclaration->getVertexSize(i),
											thisVertexData->vertexCount,
											HardwareBuffer::HBU_STATIC_WRITE_ONLY );
			thisVertexData->vertexBufferBinding->setBinding( i, vertexBuffer );

			VertexDeclaration::VertexElementList veList =
											thisVertexData->vertexDeclaration->findElementsBySource(i);

			//Grab the base submesh data
			HardwareVertexBufferSharedPtr baseVertexBuffer =
													baseVertexData->vertexBufferBinding->getBuffer(i);

			char* thisBuf = static_cast<char*>(vertexBuffer->lock(HardwareBuffer::HBL_DISCARD));
			char* baseBuf = static_cast<char*>(baseVertexBuffer->lock(HardwareBuffer::HBL_DISCARD));
			char *startBuf = baseBuf;

			const size_t sizeOfBuffer = baseVertexData->vertexCount *
											baseVertexData->vertexDeclaration->getVertexSize(i);

			//Copy and repeat
			for( size_t j=0; j<m_instancesPerBatch; ++j )
			{
				//Repeat source
				baseBuf = startBuf;

				for( size_t k=0; k<baseVertexData->vertexCount; ++k )
				{
					VertexDeclaration::VertexElementList::const_iterator it = veList.begin();
					VertexDeclaration::VertexElementList::const_iterator en = veList.end();

					while( it != en )
					{
						switch( it->getSemantic() )
						{
						case VES_BLEND_INDICES:
						*(thisBuf + it->getOffset() + 0) = *(baseBuf + it->getOffset() + 0) + j * numBones;
						*(thisBuf + it->getOffset() + 1) = *(baseBuf + it->getOffset() + 1) + j * numBones;
						*(thisBuf + it->getOffset() + 2) = *(baseBuf + it->getOffset() + 2) + j * numBones;
						*(thisBuf + it->getOffset() + 3) = *(baseBuf + it->getOffset() + 3) + j * numBones;
							break;
						default:
							memcpy( thisBuf + it->getOffset(), baseBuf + it->getOffset(), it->getSize() );
							break;
						}
						++it;
					}
					thisBuf += baseVertexData->vertexDeclaration->getVertexSize(i);
					baseBuf += baseVertexData->vertexDeclaration->getVertexSize(i);
				}
			}

			baseVertexBuffer->unlock();
			vertexBuffer->unlock();
		}
	}
	//-----------------------------------------------------------------------
	void InstanceBatchShader::getWorldTransforms( Matrix4* xform ) const
	{
		InstancedEntityVec::const_iterator itor = m_instancedEntities.begin();
		InstancedEntityVec::const_iterator end  = m_instancedEntities.end();

		while( itor != end )
		{
			xform += (*itor)->getTransforms( xform );
			++itor;
		}
	}
	//-----------------------------------------------------------------------
	unsigned short InstanceBatchShader::getNumWorldTransforms(void) const
	{
		return m_numWorldMatrices;
	}
}
