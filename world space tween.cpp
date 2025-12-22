#include "KeyBlending/BlendToWorldSpace.h"
#include "KeyBlending/CollectAnimatedObject.h"
#include "KeyBlending/KeyBlendingUtils.h"

#include <maya/MFnAnimCurve.h>
#include <maya/MDGContextGuard.h>
#include <maya/MDGContext.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTransform.h>
#include <maya/MPlug.h>
#include <maya/MQuaternion.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MGlobal.h>
#include <maya/MVector.h>
#include <maya/MEulerRotation.h>
#include <maya/MStatus.h>
#include <maya/MApiNamespace.h>

#include <cmath>

#include <qdebug.h>


std::vector<BlendToWorldSpace::AnimCurveInfo> BlendToWorldSpace::cachedAnimCurves;

void BlendToWorldSpace::clearCachedData()
{
	cachedAnimCurves.clear();
}


void BlendToWorldSpace::cacheData(MAnimCurveChange* animCurveChange)
{
	cachedAnimCurves.clear();


	std::vector<CollectAnimatedObject::AnimatedObjectInfo> animatedObjects = CollectAnimatedObject::collectAnimatedObjects();

	for (const CollectAnimatedObject::AnimatedObjectInfo& animatedObject : animatedObjects)
	{
		for (const MObject& animCurve : animatedObject.animCurves)
		{
			std::vector<KeyData> keysData = getDataFromAnimCurve(animatedObject.object, animCurve, animCurveChange);
			cachedAnimCurves.push_back(AnimCurveInfo{ animCurve, animatedObject.object, keysData });
		}
	}
}

std::vector<BlendToWorldSpace::KeyData> BlendToWorldSpace::getDataFromAnimCurve(const MObject& object, const MObject& animCurve, MAnimCurveChange* animCurveChange)
{

	KeyData keyData;

	// should to get current index first then next/prev index ( order is important )
	keyData.currentIndex = KeyBlendingUtils::getCurrentKeyIndex(animCurve, animCurveChange);

	int nextIndex = KeyBlendingUtils::findNextKeyIndexFromCurrent(animCurve);
	int prevIndex = KeyBlendingUtils::findPrevKeyIndexFromCurrent(animCurve);

	MFnAnimCurve animCurveFn(animCurve);

	double prevValue = animCurveFn.value(prevIndex);
	double nextValue = animCurveFn.value(nextIndex);

	keyData.prevValue = prevValue;
	keyData.nextValue = nextValue;


	keyData.parentInverseMatrix = getParentInverseMatrixAt(object, animCurveFn.time(keyData.currentIndex));
	keyData.prevWorldMatrix = getWorldMatrixAt(object, animCurveFn.time(prevIndex));
	keyData.nextWorldMatrix = getWorldMatrixAt(object, animCurveFn.time(nextIndex));


	return std::vector<KeyData>{ keyData };
}

MMatrix BlendToWorldSpace::getWorldMatrixAt(const MObject& object, const MTime& time)
{
	MDagPath dagPath;
	MDagPath::getAPathTo(object, dagPath);

	MDGContext ctx(time);
	MDGContextGuard contextGuard(ctx);

	// Get local transform components
	MFnTransform transformFn(object);
	
	// Get translation
	MVector translation = transformFn.getTranslation(MSpace::kTransform);
	
	// Get rotation values directly from attributes to ensure correct order
	MFnDependencyNode depNode(object);
	MPlug rotateXPlug = depNode.findPlug("rotateX", false);
	MPlug rotateYPlug = depNode.findPlug("rotateY", false);
	MPlug rotateZPlug = depNode.findPlug("rotateZ", false);
	MPlug rotateOrderPlug = depNode.findPlug("rotateOrder", false);
	
	double rotationX = rotateXPlug.asDouble();
	double rotationY = rotateYPlug.asDouble();
	double rotationZ = rotateZPlug.asDouble();
	int rotateOrder = rotateOrderPlug.asInt();
	
	// Get scale
	double scale[3];
	transformFn.getScale(scale);
	
	// Build local transformation matrix
	MTransformationMatrix localTrans;
	localTrans.setTranslation(translation, MSpace::kTransform);
	
	// Set rotation using Euler angles (rotateOrder in Maya is 0-5, MTransformationMatrix starts at kInvalid=0, kXYZ=1)
	double rotationValues[3] = { rotationX, rotationY, rotationZ };
	MTransformationMatrix::RotationOrder rotationOrder = (MTransformationMatrix::RotationOrder)(rotateOrder + 1);
	localTrans.setRotation(rotationValues, rotationOrder);
	
	localTrans.setScale(scale, MSpace::kTransform);
	MMatrix localMatrix = localTrans.asMatrix();
	
	// Get parent world matrix
	MMatrix parentWorldMatrix = MMatrix::identity;
	if (dagPath.length() > 1)
	{
		MDagPath parentPath = dagPath;
		parentPath.pop();
		parentWorldMatrix = getWorldMatrixAt(parentPath.node(), time);
	}
	
	// Combine: worldMatrix = parentWorldMatrix * localMatrix
	return localMatrix * parentWorldMatrix ;
}

MMatrix BlendToWorldSpace::getParentInverseMatrixAt(const MObject& object, const MTime& time)
{
	MDagPath dagPath;
	MDagPath::getAPathTo(object, dagPath);

	MDGContext ctx(time);
	MDGContextGuard contextGuard(ctx);

	// Get parent path
	if (dagPath.length() <= 1)
	{
		// No parent, return identity matrix
		return MMatrix::identity;
	}
	
	MDagPath parentPath = dagPath;
	parentPath.pop();
	
	// Get parent's transform components
	MFnTransform parentTransformFn(parentPath.node());
	
	// Get parent's translation
	MVector parentTranslation = parentTransformFn.getTranslation(MSpace::kTransform);
	
	// Get parent's rotation values directly from attributes to ensure correct order
	MFnDependencyNode parentDepNode(parentPath.node());
	MPlug parentRotateXPlug = parentDepNode.findPlug("rotateX", false);
	MPlug parentRotateYPlug = parentDepNode.findPlug("rotateY", false);
	MPlug parentRotateZPlug = parentDepNode.findPlug("rotateZ", false);
	MPlug parentRotateOrderPlug = parentDepNode.findPlug("rotateOrder", false);
	
	double parentRotationX = parentRotateXPlug.asDouble();
	double parentRotationY = parentRotateYPlug.asDouble();
	double parentRotationZ = parentRotateZPlug.asDouble();
	int parentRotateOrder = parentRotateOrderPlug.asInt();
	
	// Get parent's scale
	double parentScale[3];
	parentTransformFn.getScale(parentScale);
	
	// Build parent's local transformation matrix
	MTransformationMatrix parentLocalTrans;
	parentLocalTrans.setTranslation(parentTranslation, MSpace::kTransform);
	
	// Set rotation using Euler angles (rotateOrder in Maya is 0-5, MTransformationMatrix starts at kInvalid=0, kXYZ=1)
	double parentRotationValues[3] = { parentRotationX, parentRotationY, parentRotationZ };
	MTransformationMatrix::RotationOrder parentRotationOrder = (MTransformationMatrix::RotationOrder)(parentRotateOrder + 1);
	parentLocalTrans.setRotation(parentRotationValues, parentRotationOrder);
	
	parentLocalTrans.setScale(parentScale, MSpace::kTransform);
	MMatrix parentLocalMatrix = parentLocalTrans.asMatrix();
	
	// Get parent's parent world matrix (recursively)
	MMatrix parentParentWorldMatrix = MMatrix::identity;
	if (parentPath.length() > 1)
	{
		MDagPath parentParentPath = parentPath;
		parentParentPath.pop();
		parentParentWorldMatrix = getWorldMatrixAt(parentParentPath.node(), time);
	}
	
	// Build parent's world matrix: parentWorldMatrix = parentParentWorldMatrix * parentLocalMatrix
	MMatrix parentWorldMatrix = parentParentWorldMatrix * parentLocalMatrix;
	
	// Return inverse
	return parentWorldMatrix.inverse();
}

MMatrix BlendToWorldSpace::blendWorldMatrices(const MMatrix& prevMatrix, const MMatrix& nextMatrix, float blendValue)
{
	// Convert blendValue from [-1, 1] to [0, 1]
	// blendValue = -1 means 100% prev
	// blendValue = 1 means 100% next
	float t = (blendValue + 1.0f) * 0.5f;
	t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
	
	MTransformationMatrix prevTrans(prevMatrix);
	MTransformationMatrix nextTrans(nextMatrix);
	
	// ========== Translation ==========
	MVector prevTranslation = prevTrans.translation(MSpace::kWorld);
	MVector nextTranslation = nextTrans.translation(MSpace::kWorld);
	MVector blendedTranslation = prevTranslation + (nextTranslation - prevTranslation) * t;
	
	// ========== Rotation (use Quaternion SLERP) ==========
	// This is the most important part - don't blend Euler angles directly!
	MQuaternion prevQuat, nextQuat;
	prevQuat = prevTrans.rotation();
	nextQuat = nextTrans.rotation();
	
	// SLERP (Spherical Linear Interpolation) for rotation
	// Manual SLERP implementation since MQuaternion doesn't have slerp() method
	MQuaternion blendedQuat = slerpQuaternion(prevQuat, nextQuat, t);
	//MQuaternion blendedQuat = slerp(prevQuat, nextQuat, t);
	
	
	// ========== Scale ==========
	double prevScale[3], nextScale[3];
	prevTrans.getScale(prevScale, MSpace::kWorld);
	nextTrans.getScale(nextScale, MSpace::kWorld);
	
	MVector blendedScale(
		prevScale[0] + (nextScale[0] - prevScale[0]) * t,
		prevScale[1] + (nextScale[1] - prevScale[1]) * t,
		prevScale[2] + (nextScale[2] - prevScale[2]) * t
	);
	
	// ========== Reconstruct matrix ==========
	MTransformationMatrix blendedTrans;
	blendedTrans.setTranslation(blendedTranslation, MSpace::kWorld);
	blendedTrans.setRotationQuaternion(
		blendedQuat.x, 
		blendedQuat.y, 
		blendedQuat.z, 
		blendedQuat.w
	);
	double blendedScaleArray[3];
	blendedScale.get(blendedScaleArray);
	blendedTrans.setScale(blendedScaleArray, MSpace::kWorld);
	
	return blendedTrans.asMatrix();
}

void BlendToWorldSpace::worldMatrixToLocalTransform(
	const MMatrix& worldMatrix,
	const MMatrix& parentInverse,
	double& tx, double& ty, double& tz,
	double& rx, double& ry, double& rz,
	double& sx, double& sy, double& sz)
{
	// Convert to local space
	// localMatrix = worldMatrix * parentInverse
	// Manual matrix multiplication component by component
	MMatrix localMatrix;
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			double sum = 0.0;
			for (int k = 0; k < 4; k++)
			{
				sum += worldMatrix(i, k) * parentInverse(k, j);
			}
			localMatrix(i, j) = sum;
		}
	}
	
	// Decompose local matrix
	MTransformationMatrix localTrans(localMatrix);
	
	// ========== Translation ==========
	MVector translation = localTrans.translation(MSpace::kTransform);
	tx = translation.x;
	ty = translation.y;
	tz = translation.z;
	
	// ========== Rotation (Euler angles) ==========
	MEulerRotation rotation = localTrans.eulerRotation();
	rx = rotation.x;
	ry = rotation.y;
	rz = rotation.z;
	
	// ========== Scale ==========
	double scale[3];
	localTrans.getScale(scale, MSpace::kTransform);
	sx = scale[0];
	sy = scale[1];
	sz = scale[2];
}

bool BlendToWorldSpace::isTranslate(MString animCurveName)
{
	if (
		animCurveName.indexW("translateX") != -1 ||
		animCurveName.indexW("translateY") != -1 ||
		animCurveName.indexW("translateZ") != -1 
		)
	{
		return true;
	}

	return false;
}

bool BlendToWorldSpace::isRotate(MString animCurveName)
{
	if (
		animCurveName.indexW("rotateX") != -1 ||
		animCurveName.indexW("rotateY") != -1 ||
		animCurveName.indexW("rotateZ") != -1 
		)
	{
		return true;
	}

	return false;
}

bool BlendToWorldSpace::isScale(MString animCurveName)
{
	if (
		animCurveName.indexW("scaleX") != -1 ||
		animCurveName.indexW("scaleY") != -1 ||
		animCurveName.indexW("scaleZ") != -1
		)
	{
		return true;
	}

	return false;
}

void BlendToWorldSpace::tween(float sliderValue, MAnimCurveChange* animCurveChange)
{
	for (const AnimCurveInfo& animCurveInfo : cachedAnimCurves)
	{
		MFnAnimCurve animCurveFn(animCurveInfo.animCurve);

		MString animCurveName = animCurveFn.name();

		for (const KeyData& keyData : animCurveInfo.keysData)
		{
			double newValue = 0.0;

			// Check if this is a transform attribute (translate, rotate, scale)
			if (isTranslate(animCurveName) || isRotate(animCurveName) || isScale(animCurveName))
			{
				// 1. Blend matrices in world space
				MMatrix blendedWorldMatrix = blendWorldMatrices( keyData.prevWorldMatrix, keyData.nextWorldMatrix, sliderValue);

				// 2. Convert to local space
				double tx, ty, tz, rx, ry, rz, sx, sy, sz;
				worldMatrixToLocalTransform(blendedWorldMatrix, keyData.parentInverseMatrix, tx, ty, tz, rx, ry, rz, sx, sy, sz);

				// 3. Get the appropriate value based on attribute type
				if (isTranslate(animCurveName))
				{
					newValue = getTranslateValue(animCurveName, tx, ty, tz);
				}
				else if (isRotate(animCurveName))
				{
					newValue = getRotateValue(animCurveName, rx, ry, rz);
				}
				else if (isScale(animCurveName))
				{
					newValue = getScaleValue(animCurveName, sx, sy, sz);
				}
			}
			else
			{
				// For non-transform attributes, do simple linear interpolation
				// Convert sliderValue from [-1, 1] to [0, 1] for interpolation
				float t = (sliderValue + 1.0f) * 0.5f;
				t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
				newValue = lerp(keyData.prevValue, keyData.nextValue, t);
			}
			
			animCurveFn.setValue(keyData.currentIndex, newValue, animCurveChange);
		}
	}
}

double BlendToWorldSpace::getTranslateValue(const MString& animCurveName, double tx, double ty, double tz)
{
	if (animCurveName.indexW("translateX") != -1) return tx;
	if (animCurveName.indexW("translateY") != -1) return ty;
	if (animCurveName.indexW("translateZ") != -1) return tz;
	return 0.0;
}

double BlendToWorldSpace::getRotateValue(const MString& animCurveName, double rx, double ry, double rz)
{
	if (animCurveName.indexW("rotateX") != -1) return rx;
	if (animCurveName.indexW("rotateY") != -1) return ry;
	if (animCurveName.indexW("rotateZ") != -1) return rz;
	return 0.0;
}

double BlendToWorldSpace::getScaleValue(const MString& animCurveName, double sx, double sy, double sz)
{
	if (animCurveName.indexW("scaleX") != -1) return sx;
	if (animCurveName.indexW("scaleY") != -1) return sy;
	if (animCurveName.indexW("scaleZ") != -1) return sz;
	return 1.0;
}

double BlendToWorldSpace::lerp(double a, double b, float t)
{
	return a + (b - a) * t;
}

// Helper function to perform SLERP (Spherical Linear Interpolation) between two quaternions
MQuaternion BlendToWorldSpace::slerpQuaternion(const MQuaternion& q1, const MQuaternion& q2, float t)
{
	// Calculate dot product
	double dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
	
	// If dot product is negative, negate one quaternion to take the shorter path
	MQuaternion q2Adjusted = q2;
	if (dot < 0.0)
	{
		dot = -dot;
		q2Adjusted.x = -q2Adjusted.x;
		q2Adjusted.y = -q2Adjusted.y;
		q2Adjusted.z = -q2Adjusted.z;
		q2Adjusted.w = -q2Adjusted.w;
	}
	
	// Clamp dot product to [-1, 1] to avoid numerical errors
	if (dot > 1.0) dot = 1.0;
	if (dot < -1.0) dot = -1.0;
	
	// Calculate angle between quaternions
	double theta = acos(dot);
	double sinTheta = sin(theta);
	
	// If angle is very small, use linear interpolation instead
	if (sinTheta < 1e-6)
	{
		MQuaternion result;
		result.x = q1.x + (q2Adjusted.x - q1.x) * t;
		result.y = q1.y + (q2Adjusted.y - q1.y) * t;
		result.z = q1.z + (q2Adjusted.z - q1.z) * t;
		result.w = q1.w + (q2Adjusted.w - q1.w) * t;
		// Normalize
		double len = sqrt(result.x * result.x + result.y * result.y + result.z * result.z + result.w * result.w);
		if (len > 1e-6)
		{
			result.x /= len;
			result.y /= len;
			result.z /= len;
			result.w /= len;
		}
		return result;
	}
	
	// SLERP formula: slerp(q1, q2, t) = (sin((1-t)θ)/sin(θ)) * q1 + (sin(tθ)/sin(θ)) * q2
	double w1 = sin((1.0 - t) * theta) / sinTheta;
	double w2 = sin(t * theta) / sinTheta;
	
	MQuaternion result;
	result.x = w1 * q1.x + w2 * q2Adjusted.x;
	result.y = w1 * q1.y + w2 * q2Adjusted.y;
	result.z = w1 * q1.z + w2 * q2Adjusted.z;
	result.w = w1 * q1.w + w2 * q2Adjusted.w;
	
	return result;
}


