#pragma once 

#include <vector>

#include <maya/MObject.h>
#include <maya/MMatrix.h>
#include <maya/MQuaternion.h>
#include <maya/MVector.h>
#include <maya/MEulerRotation.h>
#include <maya/MTime.h>
#include <maya/MString.h>

class BlendToWorldSpace
{
public:
	static void clearCachedData();

	static void cacheData(MAnimCurveChange* animCurveChange);

	static void tween(float sliderValue, MAnimCurveChange* animCurveChange);

private:

	struct KeyData
	{
		unsigned currentIndex;

		// Store world space matrices instead of decomposed transforms
		MMatrix prevWorldMatrix;
		MMatrix nextWorldMatrix;
		
		// Store the object's parent inverse matrix for conversion back to local space
		MMatrix parentInverseMatrix;
		
		double prevValue;
		double nextValue;
	};

	struct AnimCurveInfo
	{
		MObject animCurve;
		MObject object;  // The transform object this curve belongs to
		std::vector<KeyData> keysData;
	};

	static std::vector<AnimCurveInfo> cachedAnimCurves;


	static std::vector<KeyData> getDataFromAnimCurve(const MObject& object, const MObject& animCurve, MAnimCurveChange* animCurveChange);

	// Get world space matrix at a specific time
	static MMatrix getWorldMatrixAt(const MObject& object, const MTime& time);

	// Get parent inverse matrix (for converting world space back to local space)
	static MMatrix getParentInverseMatrixAt(const MObject& object, const MTime& time);

	// Blend two world space matrices
	static MMatrix blendWorldMatrices(const MMatrix& prevMatrix, const MMatrix& nextMatrix, float blendValue);

	// Convert world space matrix to local space transform components
	static void worldMatrixToLocalTransform(const MMatrix& worldMatrix, const MMatrix& parentInverse, 
		double& tx, double& ty, double& tz,
		double& rx, double& ry, double& rz,
		double& sx, double& sy, double& sz);

	// Helper to check attribute type
	static bool isTranslate(MString animCurveName);

	static bool isRotate(MString animCurveName);

	static bool isScale(MString animCurveName);

	// Get the component value from blended transform
	static double getTranslateValue(const MString& animCurveName, double tx, double ty, double tz);
	static double getRotateValue(const MString& animCurveName, double rx, double ry, double rz);
	static double getScaleValue(const MString& animCurveName, double sx, double sy, double sz);

	// Linear interpolation helper
	static double lerp(double a, double b, float t);

	// SLERP helper function (since MQuaternion doesn't have slerp() method)
	static MQuaternion slerpQuaternion(const MQuaternion& q1, const MQuaternion& q2, float t);
};
