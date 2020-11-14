// =======================================================================================
// CameraMFD.cpp : The class of the Camera MFD.
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

#define ORBITER_MODULE

#include "CameraMFD.h"

#include <sstream>


// ==============================================================
// API interface

int mfdMode;
std::vector<MFD_Data*> mfdData;

DLLCLBK void InitModule(HINSTANCE hDLL) 
{
	static char *name = "Camera MFD";
	MFDMODESPECEX spec;
	spec.name = name;
	spec.key = OAPI_KEY_C;              // MFD mode selection key
	spec.context = nullptr;
	spec.msgproc = Camera_MFD::MsgProc; // MFD mode callback function

	mfdMode = oapiRegisterMFDMode(spec);
}

DLLCLBK void ExitModule(HINSTANCE hDLL) 
{
	oapiUnregisterMFDMode(mfdMode);
}

DLLCLBK void opcDeleteVessel(OBJHANDLE hVessel)
{
	// Delete the vessel MFD data if there are data for it
	for (size_t dataIndex = 0; dataIndex < mfdData.size(); dataIndex++)
	{
		auto data = mfdData[dataIndex];

		if (data->hVessel == hVessel)
		{
			delete data;
			mfdData.erase(mfdData.begin() + dataIndex);
		}
	}
}

DLLCLBK void opcCloseRenderViewport()
{
	// Delete all data
	for (const auto &data : mfdData)
		delete data;

	// Clear the list
	mfdData.clear();
}

// ==============================================================
// MFD class implementation

// MFD message parser
int Camera_MFD::MsgProc(UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam)
{
	// The MFD mode has been selected, so we create the MFD and return a pointer to it.
	if (msg == OAPI_MSG_MFD_OPENED)
		return int(new Camera_MFD(LOWORD(wparam), HIWORD(wparam), (VESSEL*)lparam, mfd));

	return 0;
}

bool Camera_MFD::LblClbk(void *id, char *str, void *usrdata)
{
	return static_cast<Camera_MFD*>(usrdata)->setCamLabel(str);
}

Camera_MFD::Camera_MFD(DWORD w, DWORD h, VESSEL *vessel, UINT mfd) : MFD2(w, h, vessel)
{
	// Set the default camera data
	defaultCam.label = "Camera 1";

	defaultCam.pos = defaultCam.userPos = { 0,0,0 };
	defaultCam.pitchAngle = defaultCam.userPitch = 0;
	defaultCam.yawAngle = defaultCam.userYaw = 0;
	defaultCam.rotAngle = defaultCam.userRot = 0;
	defaultCam.fov = 40;
	defaultCam.userFOV = 0;
	defaultCam.dir = { 1,0,0,0,1,0,0,0,1 };

	defaultCam.userControl = { true, true, true, true, true };
	defaultCam.multipleAdj = true;

	int mfdIndex(mfd % MAXMFD);
	bool found = false;

	// Search for any data saved for this MFD
	for (const auto &data : mfdData) 
	{
		if (data->hVessel == vessel->GetHandle() && data->mfdIndex == mfdIndex)
		{
			this->data = data;
			found = dataExist = true;
			loadConfig = false;
			break;
		}
	}

	// If no data is found, create new data
	if (!found)
	{
		data = new MFD_Data;

		data->hVessel = vessel->GetHandle();
		data->mfdIndex = mfdIndex;
		// Send the instance only if the vessel class is VESSEL3 or above (because clbkGeneric)
		data->sendInstance = vessel->Version() > 1;
		data->camMap[0] = defaultCam;

		data->cam = 0;
		data->adj = ADJ_POS;

		data->page = 0;
		data->camInfo = 1;

		mfdData.push_back(data);
	}

	setButtons();

	font = oapiCreateFont(w / 20, true, "Sans", FONT_NORMAL);

	if (ogciInitialize())
	{
		// Create 3D render target
		hRenderSrf = ogciCreateSurfaceEx(W, H, OAPISURFACE_TEXTURE  | OAPISURFACE_RENDERTARGET |
		                                       OAPISURFACE_RENDER3D | OAPISURFACE_NOMIPMAPS);
		// Clear the surface
		oapiClearSurface(hRenderSrf);

		setCustomCamera();
	}
}

Camera_MFD::~Camera_MFD()
{
	// Send the destroy message to the vessel
	if (data->sendInstance)
		static_cast<VESSEL3*>(oapiGetVesselInterface(data->hVessel))->clbkGeneric(CAMERA_MFD, data->mfdIndex, nullptr);
	
	oapiReleaseFont(font);

	if (hCamera)
		ogciDeleteCustomCamera(hCamera);

	if (hRenderSrf)
		oapiDestroySurface(hRenderSrf);
}

void Camera_MFD::ReadStatus(FILEHANDLE scn)  
{
	data->camMap.clear();

	char *line;
	int cam;
	InternalData *camData;

	while (oapiReadScenario_nextline(scn, line)) 
	{
		std::istringstream ss;
		ss.str(line);

		if (ss.str().empty())
			continue;

		if (ss.str() == "END_MFD")
			break;

		std::string id;
		
		if (ss >> id)
		{
			if (id == "CCFG")
			{
				std::string fileName;
				std::getline(ss, fileName);
				fileName.erase(0, 1);

				readConfig(fileName);

				return;
			}
			else if (id == "CADJ")
				ss >> data->adj;

			else if (id == "CPG")
				ss >> data->page;

			else if (id == "CINF")
				ss >> data->camInfo;

			else if (id == "CCAM") 
			{
				ss >> cam;
				data->camMap[cam] = defaultCam;
				camData = &data->camMap.at(cam);
			}
			else if (id == "CLBL") 
			{
				std::getline(ss, camData->label);
				camData->label.erase(0, 1);
			}
			else if (id == "CPOS")
			{
				double x, y, z;
				ss >> x; ss >> y; ss >> z;
				camData->pos = { x, y, z };
			}
			else if (id == "CPIT")
				ss >> camData->pitchAngle;

			else if (id == "CYAW")
				ss >> camData->yawAngle;

			else if (id == "CROT")
			{
				ss >> camData->rotAngle;

				if (configLoaded)
					setCamData(cam, camData->pitchAngle, camData->yawAngle, camData->rotAngle);
			}

			else if (id == "CUPOS")
			{
				double x, y, z;
				ss >> x; ss >> y; ss >> z;
				camData->userPos = { x, y, z };
			}
			else if (id == "CUPIT")
				ss >> camData->userPitch;

			else if (id == "CUYAW")
				ss >> camData->userYaw;

			else if (id == "CUROT")
			{
				ss >> camData->userRot;
				setCamData(cam, camData->pitchAngle + camData->userPitch, camData->yawAngle + camData->userYaw, camData->rotAngle + camData->userRot);
			}
			else if (id == "CFOV")
				ss >> camData->fov;

			else if (id == "CUFOV")
				ss >> camData->userFOV;

			else if (id == "CURCAM")
				ss >> data->cam;
		}
	}

	if (data->camMap.empty())
	{
		data->camMap[0] = defaultCam;
		configLoaded = false;
	}
	else
	{
		loadConfig = false;
		dataExist = true;
	}

	setButtons();
	setCustomCamera();
}

void Camera_MFD::WriteStatus(FILEHANDLE scn) const
{
	oapiWriteScenario_int(scn, "CADJ", data->adj);
	oapiWriteScenario_int(scn, "CPG", data->page);
	oapiWriteScenario_int(scn, "CINF", data->camInfo);
	oapiWriteScenario_string(scn, "", "");

	for (auto &camData : data->camMap)
	{
		oapiWriteScenario_int(scn, "CCAM", camData.first);

		if (!vesselControlled)
			oapiWriteScenario_string(scn, "CLBL", _strdup(camData.second.label.c_str()));

		if (configLoaded)
		{
			oapiWriteScenario_vec(scn, "CPOS", camData.second.pos);
			oapiWriteScenario_float(scn, "CPIT", camData.second.pitchAngle);
			oapiWriteScenario_float(scn, "CYAW", camData.second.yawAngle);
			oapiWriteScenario_float(scn, "CROT", camData.second.rotAngle);
		}

		oapiWriteScenario_vec(scn, "CUPOS", camData.second.userPos);
		oapiWriteScenario_float(scn, "CUPIT", camData.second.userPitch);
		oapiWriteScenario_float(scn, "CUYAW", camData.second.userYaw);
		oapiWriteScenario_float(scn, "CUROT", camData.second.userRot);

		if (vesselControlled)
			oapiWriteScenario_float(scn, "CUFOV", camData.second.userFOV);
		else
			oapiWriteScenario_float(scn, "CFOV", camData.second.fov);

		oapiWriteScenario_string(scn, "", "");
	}

	oapiWriteScenario_int(scn, "CURCAM", data->cam);
}

void Camera_MFD::readConfig(std::string fileName)
{
	// Set the vessel configuration file
	std::string configFile = "CameraMFD/";
	configFile += fileName;
	configFile += ".cfg";

	// Open the file
	FILEHANDLE configHandle = oapiOpenFile(configFile.c_str(), FILE_IN, CONFIG);

	if (!configHandle)
		return;

	configLoaded = true;

	ReadStatus(configHandle);

	oapiCloseFile(configHandle, FILE_IN);
}

void Camera_MFD::setButtons()
{
	buttonsLabel.clear();
	buttons.clear();
	buttonsMenu.clear();

	auto &camData = data->camMap.at(data->cam);

	switch (data->adj)
	{
	case ADJ_ROT:
		buttonsLabel.insert(buttonsLabel.end(), {
			camData.userControl.changeRot ? "LFT" : " ", camData.userControl.changeRot ? "RHT" : " ",
			" ", " ", " ", " "
			});

		buttons.insert(buttons.end(), {
			DWORD(camData.userControl.changeRot ? OAPI_KEY_A : OAPI_KEY_ESCAPE), DWORD(camData.userControl.changeRot ? OAPI_KEY_D : OAPI_KEY_ESCAPE),
			OAPI_KEY_ESCAPE, OAPI_KEY_ESCAPE, OAPI_KEY_ESCAPE, OAPI_KEY_ESCAPE
			});

		buttonsMenu.insert(buttonsMenu.end(), {
			{ camData.userControl.changeRot ? "Rotate Left" : 0, 0, camData.userControl.changeRot ? 'A' : 0 },
			{ camData.userControl.changeRot ? "Rotate Right" : 0, 0, camData.userControl.changeRot ? 'D' : 0 },
			{ nullptr }, { nullptr }, { nullptr }, { nullptr }
			});

		break;

	case ADJ_DIR:
		buttonsLabel.insert(buttonsLabel.end(), {
			camData.userControl.changeDir ? "LFT" : " ", camData.userControl.changeDir ? "RHT" : " ",
			camData.userControl.changeDir ? "UP" : " ", camData.userControl.changeDir ? "DWN" : " ",
			" ", " "
			});

		buttons.insert(buttons.end(), {
			DWORD(camData.userControl.changeDir ? OAPI_KEY_A : OAPI_KEY_ESCAPE), DWORD(camData.userControl.changeDir ? OAPI_KEY_D : OAPI_KEY_ESCAPE),
			DWORD(camData.userControl.changeDir ? OAPI_KEY_W : OAPI_KEY_ESCAPE), DWORD(camData.userControl.changeDir ? OAPI_KEY_S : OAPI_KEY_ESCAPE),
			OAPI_KEY_ESCAPE, OAPI_KEY_ESCAPE
			});

		buttonsMenu.insert(buttonsMenu.end(), {
			{ camData.userControl.changeDir ? "Look Left" : 0, 0, camData.userControl.changeDir ? 'A' : 0 },
			{ camData.userControl.changeDir ? "Look Right" : 0, 0, camData.userControl.changeDir ? 'D' : 0 },
			{ camData.userControl.changeDir ? "Look Up" : 0, 0, camData.userControl.changeDir ? 'W' : 0 },
			{ camData.userControl.changeDir ? "Look Down" : 0, 0, camData.userControl.changeDir ? 'S' : 0 },
			{ nullptr }, { nullptr }
			});

		break;

	case ADJ_POS:
		buttonsLabel.insert(buttonsLabel.end(), {
			camData.userControl.changePos ? "LFT" : " ", camData.userControl.changePos ? "RHT" : " ",
			camData.userControl.changePos ? "UP" : " ", camData.userControl.changePos ? "DWN" : " ",
			camData.userControl.changePos ? "FWD" : " ", camData.userControl.changePos ? "BCK" : " "
			});

		buttons.insert(buttons.end(), {
			DWORD(camData.userControl.changePos ? OAPI_KEY_A : OAPI_KEY_ESCAPE), DWORD(camData.userControl.changePos ? OAPI_KEY_D : OAPI_KEY_ESCAPE),
			DWORD(camData.userControl.changePos ? OAPI_KEY_W : OAPI_KEY_ESCAPE), DWORD(camData.userControl.changePos ? OAPI_KEY_S : OAPI_KEY_ESCAPE),
			DWORD(camData.userControl.changePos ? OAPI_KEY_Q : OAPI_KEY_ESCAPE), DWORD(camData.userControl.changePos ? OAPI_KEY_E : OAPI_KEY_ESCAPE)
			});

		buttonsMenu.insert(buttonsMenu.end(), {
			{ camData.userControl.changePos ? "Move Left" : 0, 0, camData.userControl.changePos ? 'A' : 0 },
			{ camData.userControl.changePos ? "Move Right" : 0, 0, camData.userControl.changePos ? 'D' : 0 },
			{ camData.userControl.changePos ? "Move Up" : 0, 0, camData.userControl.changePos ? 'W' : 0 },
			{ camData.userControl.changePos ? "Move Down" : 0, 0, camData.userControl.changePos ? 'S' : 0 },
			{ camData.userControl.changePos ? "Move Forward" : 0, 0, camData.userControl.changePos ? 'Q' : 0 },
			{ camData.userControl.changePos ? "Move Backward" : 0, 0, camData.userControl.changePos ? 'E' : 0 }
			});

		break;
	}

	switch (data->page)
	{
	case 0:
		buttonsLabel.insert(buttonsLabel.end(), {
			camData.userControl.changeFOV ? "ZM+" : " ", camData.userControl.changeFOV ? "ZM-" : " ",
			camData.multipleAdj ? "ADJ" : " ", vesselControlled ? " " : "LBL", "PG"
		});

		buttons.insert(buttons.end(), {
			DWORD(camData.userControl.changeFOV ? OAPI_KEY_Z : OAPI_KEY_ESCAPE), DWORD(camData.userControl.changeFOV ? OAPI_KEY_X : OAPI_KEY_ESCAPE),
			DWORD(camData.multipleAdj ? OAPI_KEY_J : OAPI_KEY_ESCAPE),
			DWORD(vesselControlled ? OAPI_KEY_ESCAPE : OAPI_KEY_L), OAPI_KEY_P
		});

		buttonsMenu.insert(buttonsMenu.end(), {
			{ camData.userControl.changeFOV ? "Zoom In" : 0, 0, camData.userControl.changeFOV ? 'Z' : 0 },
			{ camData.userControl.changeFOV ? "Zoom Out" : 0, 0, camData.userControl.changeFOV ? 'X' : 0 },
			{ camData.multipleAdj ? "Change Adjust Mode" : 0, 0, camData.multipleAdj ? 'J' : 0 },
			{ vesselControlled ? 0 : "Change Label", 0, vesselControlled ? 0 : 'L' },
			{ "Switch Page", 0, 'P' }
		});

		switch (data->adj)
		{
		case ADJ_ROT:
			buttonsLabel.insert(buttonsLabel.begin() + 9, camData.userControl.changeRot ? "RST" : " ");

			buttons.insert(buttons.begin() + 9, DWORD(camData.userControl.changeRot ? OAPI_KEY_R : OAPI_KEY_ESCAPE));

			buttonsMenu.insert(buttonsMenu.begin() + 9, { camData.userControl.changeRot ? "Reset Camera Rotation" : 0, 0, camData.userControl.changeRot ? 'R' : 0 });
			break;

		case ADJ_DIR:
			buttonsLabel.insert(buttonsLabel.begin() + 9, camData.userControl.changeDir ? "RST" : " ");

			buttons.insert(buttons.begin() + 9, DWORD(camData.userControl.changeDir ? OAPI_KEY_R : OAPI_KEY_ESCAPE));

			buttonsMenu.insert(buttonsMenu.begin() + 9, { camData.userControl.changeDir ? "Reset Camera Direction" : 0, 0, camData.userControl.changeDir ? 'R' : 0 });
			break;

		case ADJ_POS:
			buttonsLabel.insert(buttonsLabel.begin() + 9, camData.userControl.changePos ? "RST" : " ");

			buttons.insert(buttons.begin() + 9, DWORD(camData.userControl.changePos ? OAPI_KEY_R : OAPI_KEY_ESCAPE));

			buttonsMenu.insert(buttonsMenu.begin() + 9, { camData.userControl.changePos ? "Reset Camera Position" : 0, 0, camData.userControl.changePos ? 'R' : 0 });
			break;
		}
		break;

	case 1:
		buttonsLabel.insert(buttonsLabel.end(), {
			camData.userControl.selectCamera ? "CM+" : " ", camData.userControl.selectCamera ? "CM-" : " ",
			vesselControlled ? " " : "ADD", vesselControlled ? " " : "DEL",
			"INF", "PG"
			});

		buttons.insert(buttons.end(), {
			DWORD(camData.userControl.selectCamera ? OAPI_KEY_C : OAPI_KEY_ESCAPE), DWORD(camData.userControl.selectCamera ? OAPI_KEY_V : OAPI_KEY_ESCAPE),
			DWORD(vesselControlled ? OAPI_KEY_ESCAPE : OAPI_KEY_G), DWORD(vesselControlled ? OAPI_KEY_ESCAPE : OAPI_KEY_H),
			OAPI_KEY_I, OAPI_KEY_P
			});

		buttonsMenu.insert(buttonsMenu.end(), {
			{ camData.userControl.selectCamera ? "Next Camera" : 0, 0, camData.userControl.selectCamera ? 'C' : 0 },
			{ camData.userControl.selectCamera ? "Previous Camera" : 0, 0, camData.userControl.selectCamera ? 'V' : 0 },
			{ vesselControlled ? 0 : "Add Camera", 0, vesselControlled ? 0 : 'G' },
			{ vesselControlled ? 0 : "Delete Camera", 0, vesselControlled ? 0 : 'H' },
			{ "Change Camera Info", 0, 'I'},
			{ "Switch Page", 0, 'P' }
			});
		break;
	}
}

char *Camera_MFD::ButtonLabel(int bt)
{
	if (bt < 12)
		return buttonsLabel[bt];

	return nullptr;
}

int Camera_MFD::ButtonMenu(const MFDBUTTONMENU **menu) const
{
	if (menu)
		*menu = &(buttonsMenu[0]);

	return buttonsMenu.size();
}

bool Camera_MFD::Update(oapi::Sketchpad *skp) 
{
	// If the vessel class is VESSEL3 or higher and MFD instance wasn't sent (can't send in the constructor because the class isn't fully constructed, will result in CTD)
	if (!instanceSent)
	{
		if (data->sendInstance)
		{
			vesselControlled = static_cast<VESSEL3*>(oapiGetVesselInterface(data->hVessel))->clbkGeneric(CAMERA_MFD, data->mfdIndex, static_cast<CameraMFD*>(this)) == CAMERA_MFD;

			if (vesselControlled)
			{
				setButtons();
				InvalidateButtons();
			}
		}

		instanceSent = true;
	}

	if (loadConfig) {
		if (!vesselControlled)
			readConfig(oapiGetVesselInterface(data->hVessel)->GetClassNameA());

		loadConfig = false;
	}

	// Helper for static texts
	auto SKPTEXT = [skp](int x, int y, const char* str) { skp->Text(x, y, str, strlen(str)); };

	if (hRenderSrf && ogciSketchpadVersion(skp) == SKETCHPAD_DIRECTX)
		ogciSketchBlt(skp, hRenderSrf, 0, 0);
	else if (hRenderSrf)
		ogciRequestDXSketchpad(skp);

	Title(skp, "Camera MFD");

	// Draw a green rectangle in HUD view
	if (oapiCockpitMode() == COCKPIT_GENERIC)
		skp->Rectangle(0, 0, W, H);

	// Set the text color to green
	skp->SetTextColor(0x00FF00);

	auto &camData = data->camMap.at(data->cam);

	switch (data->camInfo)
	{
	case INFO_NONE:
		break;
	case INFO_FULL: 
	{
		skp->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::BOTTOM);

		char buffer[256];

		switch (data->adj) 
		{
		case ADJ_POS:
			sprintf_s(buffer, 256, "X: %g", camData.userPos.x);
			SKPTEXT(5, H - 60, buffer);

			sprintf_s(buffer, 256, "Y: %g", camData.userPos.y);
			SKPTEXT(5, H - 40, buffer);

			sprintf_s(buffer, 256, "Z: %g", camData.userPos.z);
			SKPTEXT(5, H - 20, buffer);

			break;
		case ADJ_DIR:
			sprintf_s(buffer, 256, "Pitch: %g°", camData.userPitch);
			SKPTEXT(5, H - 40, buffer);

			sprintf_s(buffer, 256, "Yaw: %g°", camData.userYaw);
			SKPTEXT(5, H - 20, buffer);

			break;
		case ADJ_ROT:
			sprintf_s(buffer, 256, "Rotation: %g°", camData.userRot);
			SKPTEXT(5, H - 20, buffer);

			break;
		}
	}
	case INFO_MIN:
		skp->SetTextAlign(oapi::Sketchpad::RIGHT, oapi::Sketchpad::TOP);
		SKPTEXT(W - 5, 0, camData.label.c_str());

		// Display the camera FOV if it can be changed by user
		if (camData.userControl.changeFOV) 
		{
			skp->SetTextAlign(oapi::Sketchpad::RIGHT, oapi::Sketchpad::BASELINE);

			char buffer[256];
			sprintf_s(buffer, 256, "FOV: %g", camData.fov + camData.userFOV);
			SKPTEXT(W - 5, H - 5, buffer);
		}

		// Display the camera adjust mode if the user can contorl any mode
		if (camData.userControl.changePos || camData.userControl.changeDir || camData.userControl.changeRot) 
		{
			// Set text alignment
			skp->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::BASELINE);

			// Set the adjust mode text
			switch (data->adj)
			{
			case ADJ_POS:
				SKPTEXT(5, H - 5, "Position");
				break;
			case ADJ_DIR:
				SKPTEXT(5, H - 5, "Direction");
				break;
			case ADJ_ROT:
				SKPTEXT(5, H - 5, "Rotation");
				break;
			}
		}
		break;
	}

	skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::BASELINE);

	if (!hCamera)
		SKPTEXT(W / 2, H / 2, "Custom Camera Interface Disabled");

	else if (!ogciEnabled())
		SKPTEXT(W / 2, H / 2, "No Graphics Client");

	else if (ogciSketchpadVersion(skp) != 2)
		SKPTEXT(W / 2, H / 2, "Sketchpad Not in DirectX mode");

	return true;
}

bool Camera_MFD::ConsumeButton(int bt, int event)
{
	if (event & PANEL_MOUSE_LBDOWN) 
	{
		ignoreImmediateMouse = 0;

		if (bt < 12 && buttons[bt] != OAPI_KEY_ESCAPE)
			return ConsumeKeyBuffered(buttons[bt]);
	}
	else if (event & PANEL_MOUSE_LBPRESSED) 
	{
		if (ignoreImmediateMouse < 15) 
		{
			ignoreImmediateMouse++;
			return false;
		}

		if (bt < (data->page == 0 ? 8 : 6) && buttons[bt] != OAPI_KEY_ESCAPE)
			return ConsumeKeyBuffered(buttons[bt]);
	}

	return false;
}

bool Camera_MFD::ConsumeKeyImmediate(char *kstate)
{
	if (ignoreImmediateKey < 15)
	{
		ignoreImmediateKey++;
		return false;
	}

	for (int button = 0; button < (data->page == 0 ? 8 : 6); button++)
	{
		if (KEYDOWN(kstate, buttons[button]) && buttons[button] != OAPI_KEY_ESCAPE)
		{
			immediateCall = true;
			return ConsumeKeyBuffered(buttons[button]);
		}
	}

	return false;
}

bool Camera_MFD::ConsumeKeyBuffered(DWORD key)
{
	if (immediateCall)
		immediateCall = false;

	else if (ignoreImmediateKey > 0)
	{
		if (ignoreImmediateKey >= 15) 
		{
			ignoreImmediateKey = 0;
			return false;
		}	

		ignoreImmediateKey = 0;
	}

	switch (key)
	{
	case OAPI_KEY_A:
		moveCamLeft();
		setCustomCamera();
		break;

	case OAPI_KEY_D:
		moveCamRight();
		setCustomCamera();
		break;

	case OAPI_KEY_W:
		moveCamUp();
		setCustomCamera();
		break;

	case OAPI_KEY_S:
		moveCamDown();
		setCustomCamera();
		break;

	case OAPI_KEY_Q:
		moveCamForward();
		setCustomCamera();
		break;

	case OAPI_KEY_E:
		moveCamBackward();
		setCustomCamera();
		break;

	case OAPI_KEY_Z:
	{
		if (data->page == 1)
			return false;

		auto &camData = data->camMap.at(data->cam);

		if (camData.userFOV + camData.fov <= 0.5)
			return false;

		vesselControlled ? camData.userFOV -= 0.5 : camData.fov -= 0.5;

		setCustomCamera();
		break;
	}
	case OAPI_KEY_X:
	{
		if (data->page == 1)
			return false;

		auto &camData = data->camMap.at(data->cam);

		if (camData.userFOV + camData.fov >= 80)
			return false;

		vesselControlled ? camData.userFOV += 0.5 : camData.fov += 0.5;

		setCustomCamera();
		break;
	}
	case OAPI_KEY_J:
	{
		auto &userControl = data->camMap.at(data->cam).userControl;

		while (true)
		{
			// Change the adjust mode
			data->adj >= ADJ_ROT ? data->adj = ADJ_POS : data->adj++;

			// If the new adjust mode is allowed to change by user
			if ((data->adj == ADJ_POS && userControl.changePos) || (data->adj == ADJ_DIR && userControl.changeDir) || (data->adj == ADJ_ROT && userControl.changeRot))
				break;
		}

		setButtons();
		InvalidateButtons();
		break;
	}
	case OAPI_KEY_R:
		resetCam();
		setCustomCamera();
		break;

	case OAPI_KEY_L:
		oapiOpenInputBox("Enter Camera Label:", LblClbk, _strdup(data->camMap.at(data->cam).label.c_str()), 20, this);
		break;

	case OAPI_KEY_P:
		data->page >= 1 ? data->page = 0 : data->page = 1;

		setButtons();
		InvalidateButtons();
		break;

	case OAPI_KEY_C:
	{
		auto &nextCam = data->camMap.upper_bound(data->cam);

		if (nextCam == data->camMap.end())
			return false;

		data->cam = nextCam->first;

		setButtons();
		InvalidateButtons();
		setCustomCamera();
		break;
	}
	case OAPI_KEY_V:
	{
		auto &prevCam = data->camMap.lower_bound(data->cam);
		prevCam--;

		if (prevCam == data->camMap.end())
			return false;

		data->cam = prevCam->first;

		setButtons();
		InvalidateButtons();
		setCustomCamera();
		break;
	}
	case OAPI_KEY_G:
	{
		int addedCam = data->camMap.rbegin()->first + 1;

		if (AddCamera(addedCam))
			data->cam = addedCam;

		setCustomCamera();
		break;
	}
	case OAPI_KEY_H:
		DeleteCamera(data->cam);
		break;

	case OAPI_KEY_I:
		data->camInfo >= INFO_FULL ? data->camInfo = INFO_NONE : data->camInfo++;
		break;


	default:
		return false;
	}

	InvalidateDisplay();
	return true;
}

void Camera_MFD::moveCamLeft()
{
	auto &camData = data->camMap.at(data->cam);

	switch (data->adj) 
	{
	case ADJ_POS:
		VECTOR3 dir = mul(camData.dir, _V(1, 0, 0)); normalise(dir);

		camData.userPos -= dir * 0.025;
		break;

	case ADJ_DIR:
	{
		// When dealing the left/right movement, we must reset the camera to level (no pitch). Otherwise, the camera wil move in an unexpected way.
		double pitchSin = sin(-(camData.pitchAngle + camData.userPitch) * RAD);
		double pitchCos = cos(-(camData.pitchAngle + camData.userPitch) * RAD);

		MATRIX3 mFixed = { 1, 0, 0, 0, pitchCos, pitchSin, 0, -pitchSin, pitchCos };
		camData.dir = mul(camData.dir, mFixed);

		double leftSin = sin(0.5 * RAD);
		double leftCos = cos(0.5 * RAD);

		mFixed = { leftCos, 0, -leftSin, 0, 1, 0, leftSin, 0, leftCos };
		camData.dir = mul(camData.dir, mFixed);

		camData.userYaw += 0.5;

		if (camData.userYaw > 180)
			camData.userYaw -= 360;

		pitchSin = sin((camData.pitchAngle + camData.userPitch) * RAD);
		pitchCos = cos((camData.pitchAngle + camData.userPitch) * RAD);

		mFixed = { 1, 0, 0, 0, pitchCos, pitchSin, 0, -pitchSin, pitchCos };
		camData.dir = mul(camData.dir, mFixed);

		break;
	}
	case ADJ_ROT: 
	{
		double leftSin = sin(0.5 * RAD);
		double leftCos = cos(0.5 * RAD);

		MATRIX3 mFixed = { leftCos, -leftSin, 0, leftSin, leftCos, 0, 0, 0, 1 };
		camData.dir = mul(camData.dir, mFixed);

		camData.userRot += 0.5;

		if (camData.userRot > 180)
			camData.userRot -= 360;
		break;
	}
	}
}

void Camera_MFD::moveCamRight()
{
	auto &camData = data->camMap.at(data->cam);

	switch (data->adj)
	{
	case ADJ_POS:
		VECTOR3 dir = mul(camData.dir, _V(1, 0, 0)); normalise(dir);

		camData.userPos += dir * 0.025;
		break;

	case ADJ_DIR:
	{
		double pitchSin = sin(-(camData.pitchAngle + camData.userPitch) * RAD);
		double pitchCos = cos(-(camData.pitchAngle + camData.userPitch) * RAD);

		MATRIX3 mFixed = { 1, 0, 0, 0, pitchCos, pitchSin, 0, -pitchSin, pitchCos };
		camData.dir = mul(camData.dir, mFixed);

		double rightSin = sin(-0.5 * RAD);
		double rightCos = cos(-0.5 * RAD);

		mFixed = { rightCos, 0, -rightSin, 0, 1, 0, rightSin, 0, rightCos };
		camData.dir = mul(camData.dir, mFixed);

		camData.userYaw -= 0.5;

		if (camData.userYaw < -180)
			camData.userYaw += 360;

		pitchSin = sin((camData.pitchAngle + camData.userPitch) * RAD);
		pitchCos = cos((camData.pitchAngle + camData.userPitch) * RAD);

		mFixed = { 1, 0, 0, 0, pitchCos, pitchSin, 0, -pitchSin, pitchCos };
		camData.dir = mul(camData.dir, mFixed);

		break;
	}
	case ADJ_ROT:
	{
		double rightSin = sin(-0.5 * RAD);
		double rightCos = cos(-0.5 * RAD);

		MATRIX3 mFixed = { rightCos, -rightSin, 0, rightSin, rightCos, 0, 0, 0, 1 };
		camData.dir = mul(camData.dir, mFixed);

		camData.userRot -= 0.5;

		if (camData.userRot < -180)
			camData.userRot += 360;
		break;
	}
	}
}

void Camera_MFD::moveCamUp()
{
	auto &camData = data->camMap.at(data->cam);

	switch (data->adj) 
	{
	case ADJ_POS:
		VECTOR3 dir = mul(camData.dir, _V(0, 1, 0)); normalise(dir);

		camData.userPos += dir * 0.025;
		break;

	case ADJ_DIR: 
	{
		double upSin = sin(0.5 * RAD);
		double upCos = cos(0.5 * RAD);

		MATRIX3 mFixed = { 1, 0, 0, 0, upCos, upSin, 0, -upSin, upCos };
		camData.dir = mul(camData.dir, mFixed);

		camData.userPitch += 0.5;
		if (camData.userPitch > 180)
			camData.userPitch -= 360;
		break;
	}
	}
}

void Camera_MFD::moveCamDown()
{
	auto &camData = data->camMap.at(data->cam);

	switch (data->adj) 
	{
	case ADJ_POS:
		VECTOR3 dir = mul(camData.dir, _V(0, 1, 0)); normalise(dir);

		camData.userPos -= dir * 0.025;
		break;

	case ADJ_DIR: 
	{
		double downSin = sin(-0.5 * RAD);
		double downCos = cos(-0.5 * RAD);

		MATRIX3 mFixed = { 1, 0, 0, 0, downCos, downSin, 0, -downSin, downCos };
		camData.dir = mul(camData.dir, mFixed);

		camData.userPitch -= 0.5;

		if (camData.userPitch < -180)
			camData.userPitch += 360;
		break;
	}
	}
}

void Camera_MFD::moveCamForward()
{
	auto &camData = data->camMap.at(data->cam);
	
	VECTOR3 dir = mul(camData.dir, _V(0, 0, 1)); normalise(dir);

	camData.userPos += dir * 0.025;
}

void Camera_MFD::moveCamBackward()
{
	auto &camData = data->camMap.at(data->cam);

	VECTOR3 dir = mul(camData.dir, _V(0, 0, 1)); normalise(dir);

	camData.userPos -= dir * 0.025;
}

void Camera_MFD::resetCam()
{
	auto &camData = data->camMap.at(data->cam);

	switch (data->adj) 
	{
	case ADJ_POS:
		camData.userPos = { 0,0,0 };
		break;

	case ADJ_DIR: 
	{
		camData.dir = defaultCam.dir;

		setCamData(data->cam, camData.pitchAngle, camData.yawAngle, camData.rotAngle + camData.userRot);

		camData.userPitch = 0;
		camData.userYaw = 0;
		break;
	}
	case ADJ_ROT: 
	{
		double rotSin = sin(-camData.userRot * RAD);
		double rotCos = cos(-camData.userRot * RAD);

		MATRIX3 mFixed = { rotCos, -rotSin, 0, rotSin, rotCos, 0, 0, 0, 1 };
		camData.dir = mul(camData.dir, mFixed);

		camData.userRot = 0;
		break;
	}
	}
}

bool Camera_MFD::setCamLabel(std::string label)
{
	if (label.empty() || label.size() > 20)
		return false;

	data->camMap.at(data->cam).label = label;

	return true;
}

bool Camera_MFD::CameraDataExist() { return dataExist; }

int Camera_MFD::GetCameraCount() { return int(data->camMap.size()); }

int Camera_MFD::GetCurrentCamera() { return data->cam; }

Camera_MFD::CameraData Camera_MFD::GetCameraData(int camera) 
{
	CameraData cameraData;
	cameraData.label = "";

	if (data->camMap.find(camera) == data->camMap.end())
		return cameraData;

	cameraData = data->camMap.at(camera);
	return cameraData;
}

bool Camera_MFD::SetCurrentCamera(int camera)
{
	if (data->camMap.find(camera) == data->camMap.end())
		return false;

	data->cam = camera;

	setButtons();
	InvalidateButtons();
	setCustomCamera();
	InvalidateDisplay();

	return true;
}

bool Camera_MFD::SetCameraData(int camera, CameraData cameraData)
{
	if (data->camMap.find(camera) == data->camMap.end())
		return false;

	auto &camData = data->camMap.at(camera);

	camData.label = cameraData.label;
	camData.pos = cameraData.pos;
	camData.pitchAngle = cameraData.pitchAngle;
	camData.yawAngle = cameraData.yawAngle;
	camData.rotAngle = cameraData.rotAngle;
	camData.fov = max(min(cameraData.fov, 80), 0);
	camData.dir = defaultCam.dir;

	setCamData(camera, camData.pitchAngle + camData.userPitch, camData.yawAngle + camData.userYaw, camData.rotAngle + camData.userRot);

	camData.userControl = cameraData.userControl;
	camData.multipleAdj = false;

	if (!camData.userControl.changePos && !camData.userControl.changeDir && !camData.userControl.changeRot) { }

	else if (camData.userControl.changePos && !camData.userControl.changeDir && !camData.userControl.changeRot)
		data->adj = ADJ_POS;

	else if (camData.userControl.changeDir && !camData.userControl.changePos && !camData.userControl.changeRot)
		data->adj = ADJ_DIR;

	else if (camData.userControl.changeRot && !camData.userControl.changePos && !camData.userControl.changeDir)
		data->adj = ADJ_ROT;

	else 
	{
		camData.multipleAdj = true;
		data->adj -= 1;

		while (true)
		{
			data->adj == ADJ_ROT ? data->adj = ADJ_POS : data->adj++;

			if ((data->adj == ADJ_POS && camData.userControl.changePos) || (data->adj == ADJ_DIR && camData.userControl.changeDir) || (data->adj == ADJ_ROT && camData.userControl.changeRot))
				break;
		}
	}

	setButtons();
	InvalidateButtons();
	setCustomCamera();
	InvalidateDisplay();

	return true;
}

void Camera_MFD::setCamData(int cam, double pitchAngle, double yawAngle, double rotAngle)
{
	auto &camData = data->camMap.at(cam);

	double yawSin = sin(yawAngle * RAD);
	double yawCos = cos(yawAngle * RAD);

	MATRIX3 mFixed = { yawCos, 0, -yawSin, 0, 1, 0, yawSin, 0, yawCos };
	camData.dir = mul(camData.dir, mFixed);

	double pitchSin = sin(pitchAngle * RAD);
	double pitchCos = cos(pitchAngle * RAD);

	mFixed = { 1, 0, 0, 0, pitchCos, pitchSin, 0, -pitchSin, pitchCos };
	camData.dir = mul(camData.dir, mFixed);

	double rotSin = sin(rotAngle * RAD);
	double rotCos = cos(rotAngle * RAD);

	mFixed = { rotCos, -rotSin, 0, rotSin, rotCos, 0, 0, 0, 1 };
	camData.dir = mul(camData.dir, mFixed);
}

bool Camera_MFD::AddCamera(int camera)
{
	if (data->camMap.find(camera) != data->camMap.end())
		return false;

	defaultCam.label = "Camera " + std::to_string(camera + 1);

	data->camMap[camera] = defaultCam;

	return true;
}

bool Camera_MFD::AddCamera(int camera, CameraData cameraData)
{
	if (data->camMap.find(camera) != data->camMap.end())
		return false;

	data->camMap[camera] = defaultCam;

	SetCameraData(camera, cameraData);

	return true;
}

bool Camera_MFD::DeleteCamera(int camera)
{
	if (data->camMap.size() == 1)
		return false;

	if (data->camMap.find(camera) == data->camMap.end())
		return false;

	data->camMap.erase(data->camMap.find(camera));

	auto &prevCam = data->camMap.lower_bound(data->cam);
	prevCam--;

	if (prevCam == data->camMap.end())
		data->cam = 0;

	data->cam = prevCam->first;

	setButtons();
	InvalidateButtons();
	setCustomCamera();
	InvalidateDisplay();

	return true;
}

void Camera_MFD::setCustomCamera() 
{
	auto &camData = data->camMap.at(data->cam);

	VECTOR3 dir = mul(camData.dir, _V(0, 0, 1)); normalise(dir);
	VECTOR3 rot = mul(camData.dir, _V(0, 1, 0)); normalise(rot);

	hCamera = ogciSetupCustomCamera(hCamera, data->hVessel, camData.pos + camData.userPos, dir, rot, (camData.fov + camData.userFOV) * RAD, hRenderSrf, 0xFF);
}