#pragma once
#include <Orbitersdk.h>

#define CAMERA_MFD 0x1357 // clbkGeneric Camera MFD message ID

class CameraMFD
{
public:
	// The user control policy.
	//	selectCamera: set true to allow the user to cylce between the cameras, false to not. The default value is true.
	//	changeFOV: set true to allow the user to change the camera FOV, false to not. The default value is true.
	//	changePos: set true to allow the user to change the camera position, false to not. The default value is true.
	//	changeDir: set true to allow the user to change the camera direction, false to not. The default value is true.
	//	changeRot: set true to allow the user to change the camera rotation, false to not. The default value is true.
	struct UserControl 
	{
		bool selectCamera;
		bool changeFOV;
		bool changePos;
		bool changeDir;
		bool changeRot;
	};

	// The camera data.
	//  label: the camera label. Will be empty if the struct is invalid.
	//	pos: the camera position (in the vessel local coordinates). The default value is 0,0,0.
	//	pitchAngle: the pitch angle of the camera (i.e. X axis, up/down movement) in degrees. The default value is 0.
	//	yawAngle: the yaw angle of the camera (i.e. Y axis, left/right movement) in degrees. The default value is 0.
	//	rotAngle: the rotation angle of the camera (i.e. rotating the camera picture upside down for example) in degrees. The default value is 0.
	//	fov: the camera field of view in degrees. It must be > 0 and < 80. The default value is 40 degrees.
	//	userControl: the user control policy as the UserControl struct.
	struct CameraData 
	{
		std::string label;

		VECTOR3 pos;
		double pitchAngle;
		double yawAngle;
		double rotAngle;
		double fov;

		UserControl userControl;
	};

	// Returns true if there are saved camera data for the MFD instance.
	// If there are saved data, you shouldn't add new cameras. Otherwise, undesirable camera behaviour may happen.
	// Use SetCameraData instead as detailed in the API manual.
	virtual bool CameraDataExist() = 0;

	// Returns the number of defined cameras.
	virtual int GetCameraCount() = 0;

	// Returns the current camera.
	virtual int GetCurrentCamera() = 0;

	// Returns the passed camera data, as the CameraData struct.
	// Parameters:
	//	camera: the camera number.
	// If the passed number is invalid, the struct label will be empty.
	virtual CameraData GetCameraData(int camera) = 0;

	// Sets the current camera.
	// Parameters:
	//	camera: the camera number.
	// Returns true if the camera is changed, false if the passed number is invalid.
	virtual bool SetCurrentCamera(int camera) = 0;

	// Sets the passed camera data.
	// Parameters:
	//	camera: the camera number.
	//	cameraData: the camera data as the CameraData struct.
	// Returns true if the camera data is changed, false if the passed number is invalid.
	virtual bool SetCameraData(int camera, CameraData cameraData) = 0;

	// Adds a new camera with the default view.
	// The default view position, pitch angle, and yaw angle are zeros. The default FOV is 40 degrees.
	// Parameters:
	//	camera: the camera number.
	// Returns true if the camera was added, false if the passed number already exists.
	virtual bool AddCamera(int camera) = 0;

	// Adds a new camera with a specific data.
	// Parameters:
	//	camera: the camera number.
	//	cameraData: the camare data as the CameraData struct.
	// Returns true if the camera was added, false if the passed number already exists.
	virtual bool AddCamera(int camera, CameraData cameraData) = 0;

	// Deletes the passed camera.
	// Parameters:
	//	camera: the camera number.
	// Returns true if the camera is deleted, false if the number is invalid or the the only remaining camera.
	virtual bool DeleteCamera(int camera) = 0;

	virtual ~CameraMFD() { }
};