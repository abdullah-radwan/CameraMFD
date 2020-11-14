// =======================================================================================
// CameraMFD.h : The header of the Camera MFD.
// Copyright © 2020 Abdullah Radwan. All rights reserved.
//
// This file is part of Camera MFD.
//
// Camera MFD is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Camera MFD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Camera MFD. If not, see <https://www.gnu.org/licenses/>.
//
// =======================================================================================

#pragma once
#include "CameraMFD_API.h"
#include "OGCI.h"

#include <vector>
#include <map>

struct InternalData : CameraMFD::CameraData 
{
	// The user data is used if the MFD is controlled by vessel and the user is allowed to control the camera.
	// When the vessel sets a new data for the camera, the MFD will add the user data to the new data (otherwise the user data will be lost).
	// Also when resetting the camera, only the user data will be reset. The camera will be back to the position set by vessel.
	VECTOR3 userPos;
	double userPitch;
	double userYaw;
	double userRot;
	double userFOV;

	MATRIX3 dir;
	bool multipleAdj;
};

struct MFD_Data 
{
	OBJHANDLE hVessel;
	int mfdIndex;
	bool sendInstance;
	std::map<int, InternalData> camMap;

	int cam;
	int adj;

	int page;
	int camInfo;
};

class Camera_MFD : public MFD2, public CameraMFD
{
public:
	static int MsgProc(UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam);
	static bool LblClbk(void *id, char *str, void *usrdata);
	bool setCamLabel(std::string label);

	Camera_MFD(DWORD w, DWORD h, VESSEL *vessel, UINT mfd);
	~Camera_MFD();

	void ReadStatus(FILEHANDLE scn);
	void WriteStatus(FILEHANDLE scn) const;

	bool Update(oapi::Sketchpad *skp);
	char *ButtonLabel(int bt);
	int ButtonMenu(const MFDBUTTONMENU **menu) const;

	bool ConsumeButton(int bt, int event);
	bool ConsumeKeyImmediate(char* kstate);
	bool ConsumeKeyBuffered(DWORD key);
	
	bool CameraDataExist() override;
	int GetCameraCount() override;
	int GetCurrentCamera() override;
	CameraData GetCameraData(int camera) override;

	bool SetCurrentCamera(int camera) override;
	bool SetCameraData(int camera, CameraData cameraData) override;

	bool AddCamera(int camera) override;
	bool AddCamera(int camera, CameraData cameraData) override;
	bool DeleteCamera(int camera) override;

private:
	InternalData defaultCam;

	enum AdjustMode 
	{
		ADJ_POS = 0,
		ADJ_DIR,
		ADJ_ROT
	};

	enum InfoMode 
	{
		INFO_NONE = 0,
		INFO_MIN,
		INFO_FULL
	};

	MFD_Data *data = nullptr;
	oapi::Font *font;
	SURFHANDLE hRenderSrf = nullptr;
	CAMERAHANDLE hCamera = nullptr;

	std::vector<char*> buttonsLabel;
	std::vector<DWORD> buttons;
	std::vector<MFDBUTTONMENU> buttonsMenu;

	bool instanceSent = false;     // If the MFD instance is sent to the vessel that created it
	bool loadConfig = true;        // If the MFD should load a configuration file (true if no data are saved in the scenario)
	bool configLoaded = false;     // If a config file was read and loaded
	bool dataExist = false;        // If there are saved data for this MFD instance
	bool vesselControlled = false; // If the MFD is controlled by vessel

	int ignoreImmediateMouse = 0;
	int ignoreImmediateKey = 0;
	bool immediateCall = false;

	void setButtons();
	void readConfig(std::string fileName);
	void setCamData(int cam, double pitchAngle, double yawAngle, double rotAngle);
	void setCustomCamera();

	void moveCamLeft();
	void moveCamRight();
	void moveCamUp();
	void moveCamDown();
	void moveCamForward();
	void moveCamBackward();
	void resetCam();
};