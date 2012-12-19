/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2012 Torus Knot Software Ltd

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

#include "OgreGLSLLinkProgram.h"
#include "OgreGLSLExtSupport.h"
#include "OgreGLSLGpuProgram.h"
#include "OgreGLSLProgram.h"
#include "OgreGLSLLinkProgramManager.h"
#include "OgreGL3PlusRenderSystem.h"
#include "OgreGL3PlusVertexArrayObject.h"
#include "OgreStringVector.h"
#include "OgreLogManager.h"
#include "OgreGpuProgramManager.h"
#include "OgreStringConverter.h"

namespace Ogre {

    GLint getGLGeometryInputPrimitiveType(RenderOperation::OperationType operationType, bool requiresAdjacency)
	{
		switch (operationType)
		{
            case RenderOperation::OT_POINT_LIST:
                return GL_POINTS;
            case RenderOperation::OT_LINE_LIST:
                return requiresAdjacency ? GL_LINES_ADJACENCY : GL_LINES;
            case RenderOperation::OT_LINE_STRIP:
                if(gl3wIsSupported(3, 2))
                    return requiresAdjacency ? GL_LINE_STRIP_ADJACENCY : GL_LINES;
                else
                    return requiresAdjacency ? GL_LINES_ADJACENCY : GL_LINES;
            default:
            case RenderOperation::OT_TRIANGLE_LIST:
            case RenderOperation::OT_TRIANGLE_STRIP:
                if(gl3wIsSupported(3, 2))
                    return requiresAdjacency ? GL_TRIANGLE_STRIP_ADJACENCY : GL_TRIANGLES;
                else
                    return requiresAdjacency ? GL_TRIANGLES_ADJACENCY : GL_TRIANGLES;
            case RenderOperation::OT_TRIANGLE_FAN:
                return requiresAdjacency ? GL_TRIANGLES_ADJACENCY : GL_TRIANGLES;
		}
	}
	//-----------------------------------------------------------------------
	GLint getGLGeometryOutputPrimitiveType(RenderOperation::OperationType operationType)
	{
		switch (operationType)
		{
            case RenderOperation::OT_POINT_LIST:
                return GL_POINTS;
            case RenderOperation::OT_LINE_STRIP:
                return GL_LINE_STRIP;
            case RenderOperation::OT_TRIANGLE_STRIP:
                return GL_TRIANGLE_STRIP;
            default:
                OGRE_EXCEPT(Exception::ERR_RENDERINGAPI_ERROR,
                            "Geometry shader output operation type can only be point list,"
                            "line strip or triangle strip",
                            "GLSLLinkProgram::getGLGeometryOutputPrimitiveType");
		}
	}

	//-----------------------------------------------------------------------
	GLSLLinkProgram::GLSLLinkProgram(GLSLGpuProgram* vertexProgram, GLSLGpuProgram* geometryProgram, GLSLGpuProgram* fragmentProgram, GLSLGpuProgram* hullProgram, GLSLGpuProgram* domainProgram, GLSLGpuProgram* computeProgram)
    : GLSLProgramCommon(vertexProgram, geometryProgram, fragmentProgram, hullProgram, domainProgram, computeProgram)
	{
	}

	//-----------------------------------------------------------------------
	GLSLLinkProgram::~GLSLLinkProgram(void)
	{
		glDeleteProgram(mGLProgramHandle);
        GL_CHECK_ERROR
	}
    
    void GLSLLinkProgram::_useProgram(void)
    {
		if (mLinked)
		{
            GL_CHECK_ERROR
            glUseProgram(mGLProgramHandle);
            GL_CHECK_ERROR
		}
    }

	//-----------------------------------------------------------------------
	void GLSLLinkProgram::activate(void)
	{
		if (!mLinked && !mTriedToLinkAndFailed)
		{
			mGLProgramHandle = glCreateProgram();
            GL_CHECK_ERROR

			if ( GpuProgramManager::getSingleton().canGetCompiledShaderBuffer() &&
				GpuProgramManager::getSingleton().isMicrocodeAvailableInCache(getCombinedName()) )
			{
				getMicrocodeFromCache();
			}
			else
			{
				compileAndLink();
			}

            extractLayoutQualifiers();
			buildGLUniformReferences();
		}

        _useProgram();
	}

	//-----------------------------------------------------------------------
	void GLSLLinkProgram::compileAndLink()
	{
        mVertexArrayObject = new GL3PlusVertexArrayObject();
        mVertexArrayObject->bind();

		// Compile and attach Vertex Program
		if (mVertexProgram)
        {
            if (!mVertexProgram->getGLSLProgram()->compile(true))
            {
                mTriedToLinkAndFailed = true;
                return;
            }
            mVertexProgram->getGLSLProgram()->attachToProgramObject(mGLProgramHandle);
            setSkeletalAnimationIncluded(mVertexProgram->isSkeletalAnimationIncluded());
        }

		// Compile and attach Fragment Program
		if (mFragmentProgram)
        {
            if (!mFragmentProgram->getGLSLProgram()->compile(true))
            {
                mTriedToLinkAndFailed = true;
                return;
            }
            mFragmentProgram->getGLSLProgram()->attachToProgramObject(mGLProgramHandle);
        }

        // Compile and attach Geometry Program
		if (mGeometryProgram)
		{
			if (!mGeometryProgram->getGLSLProgram()->compile(true))
			{
				return;
			}

			mGeometryProgram->getGLSLProgram()->attachToProgramObject(mGLProgramHandle);
		}

        // Compile and attach Tessellation Control Program
        if (mHullProgram)
		{
			if (!mHullProgram->getGLSLProgram()->compile(true))
			{
				return;
			}

			mHullProgram->getGLSLProgram()->attachToProgramObject(mGLProgramHandle);
		}

        // Compile and attach Tessellation Evaluation Program
        if (mDomainProgram)
		{
			if (!mDomainProgram->getGLSLProgram()->compile(true))
			{
				return;
			}

			mDomainProgram->getGLSLProgram()->attachToProgramObject(mGLProgramHandle);
		}

        // Compile and attach Compute Program
        if (mComputeProgram)
		{
			if (!mComputeProgram->getGLSLProgram()->compile(true))
			{
				return;
			}

			mComputeProgram->getGLSLProgram()->attachToProgramObject(mGLProgramHandle);
		}

		// the link
		glLinkProgram( mGLProgramHandle );
        GL_CHECK_ERROR
        glGetProgramiv( mGLProgramHandle, GL_LINK_STATUS, &mLinked );
        GL_CHECK_ERROR
	
		mTriedToLinkAndFailed = !mLinked;

		logObjectInfo( getCombinedName() + String(" GLSL link result : "), mGLProgramHandle );

        if(glIsProgram(mGLProgramHandle))
        {
            glValidateProgram(mGLProgramHandle);
        }
		logObjectInfo( getCombinedName() + String(" GLSL validation result : "), mGLProgramHandle );

		if(mLinked)
		{
			if ( GpuProgramManager::getSingleton().getSaveMicrocodesToCache() )
			{
				// add to the microcode to the cache
				String name;
				name = getCombinedName();

				// get buffer size
				GLint binaryLength = 0;
				glGetProgramiv(mGLProgramHandle, GL_PROGRAM_BINARY_LENGTH, &binaryLength);

                // create microcode
                GpuProgramManager::Microcode newMicrocode = 
                    GpuProgramManager::getSingleton().createMicrocode(binaryLength + sizeof(GLenum));

				// get binary
				glGetProgramBinary(mGLProgramHandle, binaryLength, NULL, (GLenum *)newMicrocode->getPtr(), newMicrocode->getPtr() + sizeof(GLenum));

        		// add to the microcode to the cache
				GpuProgramManager::getSingleton().addMicrocodeToCache(name, newMicrocode);
			}
		}
	}

	//-----------------------------------------------------------------------
	void GLSLLinkProgram::buildGLUniformReferences(void)
	{
		if (!mUniformRefsBuilt)
		{
			const GpuConstantDefinitionMap* vertParams = 0;
			const GpuConstantDefinitionMap* fragParams = 0;
			const GpuConstantDefinitionMap* geomParams = 0;
			const GpuConstantDefinitionMap* hullParams = 0;
			const GpuConstantDefinitionMap* domainParams = 0;
			const GpuConstantDefinitionMap* computeParams = 0;
			if (mVertexProgram)
			{
				vertParams = &(mVertexProgram->getGLSLProgram()->getConstantDefinitions().map);
			}
			if (mGeometryProgram)
			{
				geomParams = &(mGeometryProgram->getGLSLProgram()->getConstantDefinitions().map);
			}
			if (mFragmentProgram)
			{
				fragParams = &(mFragmentProgram->getGLSLProgram()->getConstantDefinitions().map);
			}
			if (mHullProgram)
			{
				hullParams = &(mHullProgram->getGLSLProgram()->getConstantDefinitions().map);
			}
			if (mDomainProgram)
			{
				domainParams = &(mDomainProgram->getGLSLProgram()->getConstantDefinitions().map);
			}
			if (mComputeProgram)
			{
				computeParams = &(mComputeProgram->getGLSLProgram()->getConstantDefinitions().map);
			}

            // Do we know how many shared params there are yet? Or if there are any blocks defined?
			GLSLLinkProgramManager::getSingleton().extractUniforms(
				mGLProgramHandle, vertParams, geomParams, fragParams, hullParams, domainParams, computeParams, mGLUniformReferences, mGLUniformBufferReferences);

			mUniformRefsBuilt = true;
		}
	}

	//-----------------------------------------------------------------------
	void GLSLLinkProgram::updateUniforms(GpuProgramParametersSharedPtr params, 
		uint16 mask, GpuProgramType fromProgType)
	{
        // Iterate through uniform reference list and update uniform values
		GLUniformReferenceIterator currentUniform = mGLUniformReferences.begin();
		GLUniformReferenceIterator endUniform = mGLUniformReferences.end();

		for (;currentUniform != endUniform; ++currentUniform)
		{
			// Only pull values from buffer it's supposed to be in (vertex or fragment)
			// This method will be called twice, once for vertex program params, 
			// and once for fragment program params.
			if (fromProgType == currentUniform->mSourceProgType)
			{
				const GpuConstantDefinition* def = currentUniform->mConstantDef;
				if (def->variability & mask)
				{
					GLsizei glArraySize = (GLsizei)def->arraySize;

					// Get the index in the parameter real list
					switch (def->constType)
					{
					case GCT_FLOAT1:
						glUniform1fv(currentUniform->mLocation, glArraySize, 
                                     params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_FLOAT2:
						glUniform2fv(currentUniform->mLocation, glArraySize, 
                                     params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_FLOAT3:
						glUniform3fv(currentUniform->mLocation, glArraySize, 
                                     params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_FLOAT4:
						glUniform4fv(currentUniform->mLocation, glArraySize, 
                                     params->getFloatPointer(def->physicalIndex));
						break;
                    case GCT_DOUBLE1:
                        glUniform1dv(currentUniform->mLocation, glArraySize,
                                     params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_DOUBLE2:
                        glUniform2dv(currentUniform->mLocation, glArraySize,
                                     params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_DOUBLE3:
                        glUniform3dv(currentUniform->mLocation, glArraySize,
                                     params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_DOUBLE4:
                        glUniform4dv(currentUniform->mLocation, glArraySize,
                                     params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_2X2:
                        glUniformMatrix2dv(currentUniform->mLocation, glArraySize,
                                           GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_2X3:
                        glUniformMatrix2x3dv(currentUniform->mLocation, glArraySize,
                                             GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_2X4:
                        glUniformMatrix2x4dv(currentUniform->mLocation, glArraySize,
                                             GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_3X2:
                        glUniformMatrix3x2dv(currentUniform->mLocation, glArraySize,
                                             GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_3X3:
                        glUniformMatrix3dv(currentUniform->mLocation, glArraySize,
                                           GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_3X4:
                        glUniformMatrix3x4dv(currentUniform->mLocation, glArraySize,
                                             GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_4X2:
                        glUniformMatrix4x2dv(currentUniform->mLocation, glArraySize,
                                             GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_4X3:
                        glUniformMatrix4x3dv(currentUniform->mLocation, glArraySize,
                                             GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
                    case GCT_MATRIX_DOUBLE_4X4:
                        glUniformMatrix4dv(currentUniform->mLocation, glArraySize,
                                           GL_TRUE, params->getDoublePointer(def->physicalIndex));
                        break;
					case GCT_MATRIX_2X2:
						glUniformMatrix2fv(currentUniform->mLocation, glArraySize,
                                           GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_2X3:
                        glUniformMatrix2x3fv(currentUniform->mLocation, glArraySize, 
                                             GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_2X4:
                        glUniformMatrix2x4fv(currentUniform->mLocation, glArraySize, 
                                             GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_3X2:
                        glUniformMatrix3x2fv(currentUniform->mLocation, glArraySize, 
                                             GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_3X3:
						glUniformMatrix3fv(currentUniform->mLocation, glArraySize, 
                                           GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_3X4:
                        glUniformMatrix3x4fv(currentUniform->mLocation, glArraySize, 
                                             GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_4X2:
                        glUniformMatrix4x2fv(currentUniform->mLocation, glArraySize, 
                                             GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_4X3:
                        glUniformMatrix4x3fv(currentUniform->mLocation, glArraySize, 
                                             GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_MATRIX_4X4:
						glUniformMatrix4fv(currentUniform->mLocation, glArraySize, 
                                           GL_TRUE, params->getFloatPointer(def->physicalIndex));
						break;
					case GCT_INT1:
						glUniform1iv(currentUniform->mLocation, glArraySize, 
                                     (GLint*)params->getIntPointer(def->physicalIndex));
						break;
					case GCT_INT2:
						glUniform2iv(currentUniform->mLocation, glArraySize, 
                                     (GLint*)params->getIntPointer(def->physicalIndex));
						break;
					case GCT_INT3:
						glUniform3iv(currentUniform->mLocation, glArraySize, 
                                     (GLint*)params->getIntPointer(def->physicalIndex));
						break;
					case GCT_INT4:
						glUniform4iv(currentUniform->mLocation, glArraySize, 
                                     (GLint*)params->getIntPointer(def->physicalIndex));
						break;
					case GCT_SAMPLER1D:
					case GCT_SAMPLER1DSHADOW:
					case GCT_SAMPLER2D:
					case GCT_SAMPLER2DSHADOW:
                    case GCT_SAMPLER2DARRAY:
					case GCT_SAMPLER3D:
					case GCT_SAMPLERCUBE:
                    case GCT_SAMPLERRECT:
						// Samplers handled like 1-element ints
						glUniform1iv(currentUniform->mLocation, 1, 
                                     (GLint*)params->getIntPointer(def->physicalIndex));
						break;
                    case GCT_SUBROUTINE:
                    case GCT_UNKNOWN:
                        break;

					} // End switch

                    GL_CHECK_ERROR
				} // Variability & mask
			} // fromProgType == currentUniform->mSourceProgType

  		} // End for
	}
	//-----------------------------------------------------------------------
	void GLSLLinkProgram::updateUniformBlocks(GpuProgramParametersSharedPtr params, 
                                         uint16 mask, GpuProgramType fromProgType)
	{
        // Iterate through the list of uniform buffers and update them as needed
		GLUniformBufferIterator currentBuffer = mGLUniformBufferReferences.begin();
		GLUniformBufferIterator endBuffer = mGLUniformBufferReferences.end();

        const GpuProgramParameters::GpuSharedParamUsageList& sharedParams = params->getSharedParameters();

		GpuProgramParameters::GpuSharedParamUsageList::const_iterator it, end = sharedParams.end();
		for (it = sharedParams.begin(); it != end; ++it)
        {
            for (;currentBuffer != endBuffer; ++currentBuffer)
            {
                GL3PlusHardwareUniformBuffer* hwGlBuffer = static_cast<GL3PlusHardwareUniformBuffer*>(currentBuffer->get());
                GpuSharedParametersPtr paramsPtr = it->getSharedParams();

                // Block name is stored in mSharedParams->mName of GpuSharedParamUsageList items
                GLint UniformTransform = glGetUniformBlockIndex(mGLProgramHandle, it->getName().c_str());
                GL_CHECK_ERROR
                glUniformBlockBinding(mGLProgramHandle, UniformTransform, hwGlBuffer->getGLBufferBinding());
                GL_CHECK_ERROR

                hwGlBuffer->writeData(0, hwGlBuffer->getSizeInBytes(), paramsPtr->getFloatConstantList().data());
            }
        }
	}
	//-----------------------------------------------------------------------
	void GLSLLinkProgram::updatePassIterationUniforms(GpuProgramParametersSharedPtr params)
	{
		if (params->hasPassIterationNumber())
		{
			size_t index = params->getPassIterationNumberIndex();

			GLUniformReferenceIterator currentUniform = mGLUniformReferences.begin();
			GLUniformReferenceIterator endUniform = mGLUniformReferences.end();

			// Need to find the uniform that matches the multi pass entry
			for (;currentUniform != endUniform; ++currentUniform)
			{
				// Get the index in the parameter real list
				if (index == currentUniform->mConstantDef->physicalIndex)
				{
					glUniform1fv(currentUniform->mLocation, 1, params->getFloatPointer(index));
                    GL_CHECK_ERROR
					// There will only be one multipass entry
					return;
				}
			}
		}
    }

} // namespace Ogre
