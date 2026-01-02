#include "TempPivot/TempPivot.h"
#include "MayaUtils/MayaMacros.h"
#include <MayaUtils/MayaUtils.h>

#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MModelMessage.h>
#include <maya/MPlug.h>
#include <maya/MObjectHandle.h>
#include <maya/MFnTransform.h>
#include <maya/MItSelectionList.h>
#include <maya/MAnimMessage.h>
#include <maya/MEventMessage.h>
#include <maya/MDGMessage.h>
#include <maya/MDagPathArray.h>
#include <maya/MNodeMessage.h>
#include <maya/MPoint.h>
#include <maya/MAngle.h>

#include <QtCore/qfile.h>

TempPivot& TempPivot::getInstance()
{
	static TempPivot tempPivotInstance;
	return tempPivotInstance;
}

TempPivot::TempPivot()
{
	executePythonScript();
}

TempPivot::~TempPivot()
{
	cleanUp();
}

void TempPivot::resetPivot()
{
	if (!isNodeValid(pivotTransform.node()))
	{
		MayaUtils::inViewMessage("Temp Pivot Node Doesn't Exists.");
		return;
	}

	MFnTransform tempPivotFn(pivotTransform);
	tempPivotFn.setRotatePivot(MPoint(0, 0, 0), MSpace::kTransform, true);
	tempPivotFn.setScalePivot(MPoint(0, 0, 0), MSpace::kTransform, true);

	updateRelativeMatrices();
	ctxEditMode();
}

void TempPivot::toggle()
{
	enableState = !enableState;
	if (enableState)
	{
		create();
	}
	else
	{
		cleanUp();
	}
}

void TempPivot::create()
{
	MStatus status = getActiveSelection();
	if (!status) return;

	setupSelectionConnection();
	createTempPivotNode();
	updateTempPosition();
	addCallbacks();
	ctxEditMode();
}

void TempPivot::cleanUp()
{
	removeCallbacks();
	removeSelectionConnection();
	deleteTempPivotNode();
	MGlobal::executeCommand("setToolTo \"moveSuperContext\"");
	
	MGlobal::setActiveSelectionList(MSelectionList());
	for (ObjectInfo& objectInfo : objectsInfo)
		MGlobal::selectByName(objectInfo.object.fullPathName(), MGlobal::kAddToList);

	objectsInfo.clear();
	enableState = false;
}

MStatus TempPivot::getActiveSelection()
{
	MSelectionList activeSelection;
	MGlobal::getActiveSelectionList(activeSelection);
	if (!activeSelection.length())
	{
		MayaUtils::inViewMessage("Select One or More Objects");
		return MS::kFailure;
	}
	
	MItSelectionList it(activeSelection);
	for (; !it.isDone(); it.next())
	{
		MDagPath object;
		it.getDagPath(object);

		if (object.node().apiType() != MFn::kTransform)
			object.pop();

		objectsInfo.push_back(ObjectInfo{ object });
	}

	return MStatus::kSuccess;
}

void TempPivot::createTempPivotNode()
{
	deleteTempPivotNode();

	MObject pivotTransformNode = MFnDagNode().create("transform", kPivotTransformName);

	MFnDependencyNode nodeFn(pivotTransformNode);
	nodeFn.findPlug("visibility", false).setKeyable(false);

	MPlug translate = nodeFn.findPlug("translate", false);
	MPlug rotate = nodeFn.findPlug("rotate", false);
	MPlug scale = nodeFn.findPlug("scale", false);

	for (unsigned i = 0; i < translate.numChildren(); i++)
	{
		//translate.child(i).setKeyable(false);
		//rotate.child(i).setKeyable(false);
		//scale.child(i).setKeyable(false);
	}

	MDagPath::getAPathTo(pivotTransformNode, pivotTransform);
}

void TempPivot::applyTransformations()
{
	MMatrix pivotWorldMatrix = pivotTransform.inclusiveMatrix();
	for (const ObjectInfo& objectInfo : objectsInfo)
	{
		MMatrix newWorldMatrix = objectInfo.relativeMatrix * pivotWorldMatrix;
		//MMatrix localMatrix = newWorldMatrix * objectInfo.object.exclusiveMatrixInverse();
		//MFnTransform(objectInfo.object).set(localMatrix);
		MGlobal::executeCommand("xform -worldSpace -matrix " + matrixToRecord(newWorldMatrix) + " \"" + objectInfo.object.fullPathName() + "\"", false, true);
	}
}

void TempPivot::updateRelativeMatrices()
{
	MMatrix pivotWorldMatrixInverse = pivotTransform.inclusiveMatrixInverse();
	for (ObjectInfo& objectInfo : objectsInfo)
	{
		objectInfo.relativeMatrix = objectInfo.object.inclusiveMatrix() * pivotWorldMatrixInverse;
	}
}

void TempPivot::updateTempPosition()
{
	if (!isNodeValid(objectsInfo[0].object.node()) || !isNodeValid(pivotTransform.node()))
	{
		cleanUp();
		return;
	}

	removeAttributeChangedCallback();

	MFnTransform tempPivotFn(pivotTransform);

	MPoint rotatePivot = tempPivotFn.rotatePivot(MSpace::kTransform);
	
	tempPivotFn.setRotatePivot(MPoint(0, 0, 0), MSpace::kTransform, true);
	tempPivotFn.setScalePivot(MPoint(0, 0, 0), MSpace::kTransform, true);

	tempPivotFn.set(objectsInfo[0].object.inclusiveMatrix());

	tempPivotFn.setRotatePivot(rotatePivot, MSpace::kTransform, true);
	tempPivotFn.setScalePivot(rotatePivot, MSpace::kTransform, true);

	updateRelativeMatrices();
	addAttributeChangedCallback();
}


// -------------------------------------------------- Callbacks -------------------------------------------------

void TempPivot::addCallbacks()
{
	if (callbackIds.length())
		return;

	callbackIds.append(MDGMessage::addTimeChangeCallback([](MTime&, void* c) {((TempPivot*)c)->addIdleCallback(); }, this));
	callbackIds.append(MAnimMessage::addAnimCurveEditedCallback([](MObjectArray&, void* c) {((TempPivot*)c)->addIdleCallback(); }, this));
	callbackIds.append(MEventMessage::addEventCallback("PreFileNewOrOpened", [](void* c) {((TempPivot*)c)->cleanUp(); }, this));
	callbackIds.append(MModelMessage::addCallback(MModelMessage::kActiveListModified, onActiveSelectionChanged, this));

	addAttributeChangedCallback();
}
void TempPivot::removeCallbacks()
{
	removeAttributeChangedCallback();
	MMessage::removeCallback(idleCallbackId);
	MMessage::removeCallbacks(callbackIds);
	callbackIds.clear();
}

void TempPivot::addIdleCallback()
{
	MMessage::removeCallback(idleCallbackId);
	idleCallbackId = MEventMessage::addEventCallback("idle", [](void* clientData)
		{
			TempPivot* tempPivot = (TempPivot*)clientData;
			MMessage::removeCallback(tempPivot->idleCallbackId);
			tempPivot->updateTempPosition();
		}
		, this
	);
}

void TempPivot::addAttributeChangedCallback()
{
	if (AttributeChangedCallbackId == 0)
	{
		MObject node = pivotTransform.node();
		AttributeChangedCallbackId = MNodeMessage::addAttributeChangedCallback(node, onAttributeChanged, this);
	}
}
void TempPivot::removeAttributeChangedCallback()
{
	MMessage::removeCallback(AttributeChangedCallbackId);
	AttributeChangedCallbackId = 0;
}



void TempPivot::onActiveSelectionChanged(void* clientData)
{
	TempPivot* tempPivot = (TempPivot*)clientData;

	MSelectionList activeSelection;
	MGlobal::getActiveSelectionList(activeSelection);

	MDagPathArray selected;
	MItSelectionList it(activeSelection, MFn::kDependencyNode);
	for (; !it.isDone(); it.next())
	{
		MDagPath dag;
		it.getDagPath(dag);
		selected.append(dag);
	}

	if (selected.length() == 0 || selected.length() >= 2 || selected[0].node() != tempPivot->pivotTransform.node() )
	{
		tempPivot->cleanUp();
	}
}

void TempPivot::onAttributeChanged(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData)
{
	TempPivot* tempPivot = (TempPivot*)clientData;

	if (!(msg & MNodeMessage::kAttributeSet))
		return;

	if (tempPivot->isTransform(plug.partialName()))
	{
		tempPivot->applyTransformations();
	}
}







// -------------------------------------------------- Helpers -------------------------------------------------

void TempPivot::deleteTempPivotNode()
{
	MSelectionList selList;
	MStatus status = selList.add(kPivotTransformName);
	if (status == MS::kSuccess)
	{
		MObject node;
		selList.getDependNode(0, node);

		if(isNodeValid(node))
			MGlobal::deleteNode(node);
	}
}

bool TempPivot::isNodeValid(const MObject& node)
{
	MObjectHandle objHandle(node);
	return (objHandle.isValid() && objHandle.isAlive());
}

void TempPivot::executePythonScript()
{
	QFile file(":TempPivotUtils.py");
	file.open(QIODevice::ReadOnly | QIODevice::Text);

	QByteArray fileContent = file.readAll();

	file.close();

	MGlobal::executePythonCommand(fileContent.toStdString().c_str());
}

bool TempPivot::isTransform(const MString& plugName)
{
	for (MString attribute : {"t", "tx", "ty", "tz", "r", "rx", "ry", "rz", "s", "sx", "sy", "sz"})
	{
		if (plugName == attribute)
			return true;
	}
	return false;
}

void TempPivot::setupSelectionConnection()
{
	MString selectionPyList = selectionToPyList();
	MGlobal::executePythonCommand(
		MString("setup_selection_connection( \"") + kSelectionConnectionName + "\", " + selectionPyList + ")" );

	MGlobal::executeCommand("toggleAutoLoad graphEditor1OutlineEd false");
}

void TempPivot::removeSelectionConnection()
{
	MGlobal::executePythonCommand(MString("remove_selection_connection(\"") + kSelectionConnectionName + "\")");
	MGlobal::executeCommand("toggleAutoLoad graphEditor1OutlineEd true");
}

MString TempPivot::selectionToPyList()
{
	MString pyList = "[ ";

	for (const ObjectInfo& objectInfo : objectsInfo)
	{
		pyList += "\"" + objectInfo.object.fullPathName() + "\", ";
	}

	return pyList.substring(0, pyList.length() - 3) + "]";
}

MString TempPivot::matrixToRecord(const MMatrix& matrix)
{
	MString matrixArrayStr;

	for (size_t i = 0; i < 4; i++)
	{
		for (size_t j = 0; j < 4; j++)
		{
			MString s;
			s.set(matrix[i][j], 15);
			matrixArrayStr += s + " ";
		}
	}

	return matrixArrayStr;
}

void TempPivot::ctxEditMode()
{
	MGlobal::executeCommand("select " + pivotTransform.fullPathName() + ";setToolTo \"moveSuperContext\"; ctxEditMode;");
}

MMatrix TempPivot::getPivotMatrix(const MDagPath& object)
{
	MVector rotatePivote = MFnTransform(object).rotatePivot(MSpace::kWorld);

	MMatrix pivotMatrix;
	pivotMatrix[3][0] = rotatePivote.x;
	pivotMatrix[3][1] = rotatePivote.y;
	pivotMatrix[3][2] = rotatePivote.z;

	return pivotMatrix;
}


