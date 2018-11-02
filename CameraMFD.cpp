#define STRICT
#define ORBITER_MODULE
#include <windows.h>
#include <sstream>

#include "orbitersdk.h"
#include "gcAPI.h"
#include "CameraMFD.h"
#include "Sketchpad2.h"

// ==============================================================
// API interface

int g_MFDmode;  ///< identifier for new MFD mode

DLLCLBK void InitModule (HINSTANCE hDLL) {
	static char *name = "Camera MFD";    // MFD mode name
	MFDMODESPECEX spec;
	spec.name = name;
	spec.key = OAPI_KEY_C;               // MFD mode selection key
	spec.context = NULL;
	spec.msgproc = CameraMFD::MsgProc;   // MFD mode callback function

	// Register the new MFD mode with Orbiter
	g_MFDmode = oapiRegisterMFDMode(spec);
}

DLLCLBK void ExitModule (HINSTANCE hDLL) {
	// Unregister the custom MFD mode when the module is unloaded
	oapiUnregisterMFDMode(g_MFDmode);
}

// ==============================================================
// MFD class implementation

// --- Class Statics ---
std::map<int, Info> CameraMFD::info;

// MFD message parser
int CameraMFD::MsgProc (UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam) {
	switch (msg) {
	case OAPI_MSG_MFD_OPENED:
		// Our new MFD mode has been selected, so we create the MFD and
		// return a pointer to it.
		return (int)(new CameraMFD(LOWORD(wparam), HIWORD(wparam), (VESSEL*)lparam, mfd));
	}
	return 0;
}

// --- Constructor ---
CameraMFD::CameraMFD (DWORD w, DWORD h, VESSEL *vessel, UINT mfd)
	: MFD2(w, h, vessel), mfdIndex(mfd % MAXMFD), hRenderSrf(NULL), hCamera(NULL) {

	font = oapiCreateFont(w/20, true, "Sans", FONT_NORMAL);

	if (info[mfdIndex].camVector.empty()) {
		info[mfdIndex].camVector.push_back({ VECTOR3{ 0,0,0 }, VECTOR3{ 0,0,1 }, VECTOR3{ 0,1,0 }, 40 });
		info[mfdIndex].adj = 0;
		info[mfdIndex].cam = 0;
	}

	if (gcInitialize()) {
		// Create 3D render target
		hRenderSrf = oapiCreateSurfaceEx(W, H, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET |
		                                                       OAPISURFACE_RENDER3D | OAPISURFACE_NOMIPMAPS);
		// Clear the surface
		oapiClearSurface(hRenderSrf);

		SetupCustomCamera();
	}
}

// Read status from scenario file
void CameraMFD::ReadStatus(FILEHANDLE scn) {

	auto data = &info[mfdIndex];
	std::string c, x, y, z;
	int cam;
	char *line;

	data->camVector.clear();

	while (oapiReadScenario_nextline(scn, line)) {
		std::istringstream ss;
		ss.str(line);
		std::string id;

		if (ss >> id) {
			if (id == "CCAM") {
				ss >> c;
				cam = std::stoi(c);
				data->camVector.insert(data->camVector.begin() + cam, { VECTOR3{}, VECTOR3{}, VECTOR3{}, 0 });
			}
			else if (id == "CPOS") {
				ss >> x; ss >> y; ss >> z;
				data->camVector[cam].pos = VECTOR3{ std::stod(x), std::stod(y), std::stod(z) };
			}
			else if (id == "CDIR") {
				ss >> x; ss >> y; ss >> z;
				data->camVector[cam].dir = VECTOR3{ std::stod(x), std::stod(y), std::stod(z) };
			}
			else if (id == "CROT") {
				ss >> x; ss >> y; ss >> z;
				data->camVector[cam].rot = VECTOR3{ std::stod(x), std::stod(y), std::stod(z) };
			}
			else if (id == "CFOV") ss >> data->camVector[cam].fov;
			else if (id == "CADJ") ss >> data->adj;
			else if (id == "CURCAM") ss >> data->cam;
		}
	}

	if (data->camVector.empty()) {
		data->camVector.push_back({ VECTOR3{ 0,0,0 }, VECTOR3{ 0,0,1 }, VECTOR3{ 0,1,0 }, 40 });
		data->adj = 0;
		data->cam = 0;
	}

	SetupCustomCamera();
}

// Repaint the MFD page
bool CameraMFD::Update(oapi::Sketchpad *skp) {
	// Helper for static texts
	auto SKPTEXT = [skp](int x, int y, const char *str) { skp->Text(x, y, str, strlen(str)); };
	#define SKPTEXT(x,y,str) (skp->Text(x, y, str, sizeof(str)-1))

	if (hRenderSrf && gcSketchpadVersion(skp) == 2) {
		Sketchpad2 *skp2 = (Sketchpad2 *)skp;

		// Blit the camera view into the sketchpad.
		RECT sr = { 0, 0, W, H };
		skp2->CopyRect(hRenderSrf, &sr, 0, 0);

	}

	auto data = info[mfdIndex];

	// Draw an MFD title
	Title(skp, "Camera MFD");

	// Draw a green rectangle in HUD view
	if(oapiCockpitMode() == COCKPIT_GENERIC) skp->Rectangle(0, 0, W, H);

	// Set color to green
	skp->SetTextColor(0x00FF00);

	// Set text alignment
	skp->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::BASELINE);

	// Set the adjust mode text
	switch (data.adj) {
	case 0:
		SKPTEXT(5, H - 5, "Position");
		break;
	case 1:
		SKPTEXT(5, H - 5, "Direction");
		break;
	case 2:
		SKPTEXT(5, H - 5, "Rotation");
		break;
	default:
		break;
	}

	skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::BASELINE);
	if (!hCamera) SKPTEXT(W / 2, H / 2, "Custom camera interface disabled");
	else if (!gcEnabled()) SKPTEXT(W / 2, H / 2, "No Graphics Client");
	else if (gcSketchpadVersion(skp) != 2) SKPTEXT(W / 2, H / 2, "Sketchpad Not in DirectX mode");

	return true;
}

// Return button labels
char *CameraMFD::ButtonLabel (int bt) {
	// The labels for the buttons used by our MFD mode
	static char *label[] = { "RHT", "LFT", "FWD", "BCK", "UP", "DWN", "FOV", "ADJ", "RST", "CAM", "ADD", "REM"};
	return (bt < ARRAYSIZE(label)) ? label[bt] : NULL;
}

// Return button menus
int CameraMFD::ButtonMenu (const MFDBUTTONMENU **menu) const {
	// The menu descriptions for the buttons
	static const MFDBUTTONMENU mnu[] = {
		{ "Move camera right"    , 0   , 'R' },
		{ "Move camera left"     , 0   , 'L' },
		{ "Move camera forward"  , 0   , 'F' },
		{ "Move camera backward" , 0   , 'B' },
		{ "Move camera up"       , 0   , 'U' },
		{ "Move camera down"     , 0   , 'D' },
		{ "Field of view"        , 0   , 'Z' },
		{ "Adjust mode"          , 0   , 'J' },
		{ "Reset coords"		 , 0   , 'S' },
		{ "Add a camera"         , 0   , 'N' },
		{ "Remove camera"		 , 0   , 'E' }
	};
	if (menu) *menu = mnu;
	return ARRAYSIZE(mnu); // return the number of buttons used
}

// Button handler
bool  CameraMFD::ConsumeButton (int bt, int event) {
	if (event & PANEL_MOUSE_LBDOWN) {
		static const DWORD btkey[] = {
			OAPI_KEY_R, OAPI_KEY_L, // Right and left
			OAPI_KEY_F, OAPI_KEY_B, // Forward and backward
			OAPI_KEY_U, OAPI_KEY_D, // Up and down
			OAPI_KEY_Z,				// Field of view
			OAPI_KEY_J,				// Adjust
			OAPI_KEY_S,				// Reset
			OAPI_KEY_C,				// Set camera
			OAPI_KEY_N, OAPI_KEY_E  // Add and remove
		};
		if (bt < ARRAYSIZE(btkey) && btkey[bt] != NULL) { return ConsumeKeyBuffered(btkey[bt]); }
	}
	return false;
}

// Key handler
bool CameraMFD::ConsumeKeyBuffered (DWORD key) {

	auto data = &info[mfdIndex];

	switch (key) {
	case OAPI_KEY_J:
		if (data->adj == 2) data->adj = 0;
		else data->adj += 1;
		break;
	case OAPI_KEY_C:
		// If the cam is below the last index
		if (data->cam < data->camVector.size() - 1) data->cam += 1;
		else data->cam = 0;
		SetupCustomCamera();
		break;
	case OAPI_KEY_N:
		data->cam += 1;
		data->camVector.push_back({ VECTOR3{ 0,0,0 }, VECTOR3{ 0,0,1 }, VECTOR3{ 0,1,0 }, 40 });
		SetupCustomCamera();
		break;
	case OAPI_KEY_E:
		// If there only one camera
		if (data->camVector.size() == 1) break;
		data->camVector.erase((data->camVector.begin() + data->cam));
		data->cam = 0;
		SetupCustomCamera();
		break;
	case OAPI_KEY_R:
		switch (data->adj) {
		case 0:
			data->camVector[data->cam].pos.x += 0.5;
			break;
		case 1:
			data->camVector[data->cam].dir.x += 0.5;
			break;
		case 2:
			data->camVector[data->cam].rot.x += 0.5;
			break;
		default:
			break;
		}
		SetupCustomCamera();
		break;
	case OAPI_KEY_L:
		switch (data->adj) {
		case 0:
			data->camVector[data->cam].pos.x -= 0.5;
			break;
		case 1:
			data->camVector[data->cam].dir.x -= 0.5;
			break;
		case 2:
			data->camVector[data->cam].rot.x -= 0.5;
			break;
		default:
			break;
		}
		SetupCustomCamera();
		break;
	case OAPI_KEY_F:
		switch (data->adj) {
		case 0:
			data->camVector[data->cam].pos.y += 0.5;
			break;
		case 1:
			data->camVector[data->cam].dir.y += 0.5;
			break;
		case 2:
			data->camVector[data->cam].rot.y += 0.5;
			break;
		default:
			break;
		}
		SetupCustomCamera();
		break;
	case OAPI_KEY_B:
		switch (data->adj) {
		case 0:
			data->camVector[data->cam].pos.y -= 0.5;
			break;
		case 1:
			data->camVector[data->cam].dir.y -= 0.5;
			break;
		case 2:
			data->camVector[data->cam].rot.y -= 0.5;
			break;
		default:
			break;
		}
		SetupCustomCamera();
		break;
	case OAPI_KEY_U:
		switch (data->adj) {
		case 0:
			data->camVector[data->cam].pos.z += 0.5;
			break;
		case 1:
			data->camVector[data->cam].dir.z += 0.5;
			break;
		case 2:
			data->camVector[data->cam].rot.z += 0.5;
			break;
		default:
			break;
		}
		SetupCustomCamera();
		break;
	case OAPI_KEY_D:
		switch (data->adj) {
		case 0:
			data->camVector[data->cam].pos.z -= 0.5;
			break;
		case 1:
			data->camVector[data->cam].dir.z -= 0.5;
			break;
		case 2:
			data->camVector[data->cam].rot.z -= 0.5;
			break;
		default:
			break;
		}
		SetupCustomCamera();
		break;
	case OAPI_KEY_Z:
		if (data->camVector[data->cam].fov == 60) data->camVector[data->cam].fov = 10;
		else data->camVector[data->cam].fov += 10;
		SetupCustomCamera();
		break;
	case OAPI_KEY_S:
		switch (data->adj) {
		case 0:
			data->camVector[data->cam].pos = { 0,0,0 };
			break;
		case 1:
			data->camVector[data->cam].dir = { 0,0,1 };
			break;
		case 2:
			data->camVector[data->cam].rot = { 0,1,0 };
			break;
		default:
			break;
		}
		data->camVector[data->cam].fov = 40;
		SetupCustomCamera();
		break;
	default:
		return false;
	}

	InvalidateDisplay();
	return true;
}

// Apply changes to the (current) custom camera object
void CameraMFD::SetupCustomCamera () {

	auto data = &info[mfdIndex];

	hCamera = gcSetupCustomCamera( hCamera, pV->GetHandle(),
		data->camVector[data->cam].pos, data->camVector[data->cam].dir, data->camVector[data->cam].rot,
		data->camVector[data->cam].fov * RAD, hRenderSrf, 0xFF );
}

// Write status to scenario file
void CameraMFD::WriteStatus(FILEHANDLE scn) const {

	auto data = &info[mfdIndex];
		
	for (int cam = 0; cam < data->camVector.size(); cam++) {
		oapiWriteScenario_int(scn, "CCAM", cam);
		oapiWriteScenario_vec(scn, "CPOS", data->camVector[cam].pos);
		oapiWriteScenario_vec(scn, "CDIR", data->camVector[cam].dir);
		oapiWriteScenario_vec(scn, "CROT", data->camVector[cam].rot);
		oapiWriteScenario_int(scn, "CFOV", data->camVector[cam].fov);
	}
	oapiWriteScenario_int(scn, "CADJ", data->adj);
	oapiWriteScenario_int(scn, "CURCAM", data->cam);
}

// --- Destructor ---
CameraMFD::~CameraMFD() {
	oapiReleaseFont(font);
	// Attention, Always delete the camera before the surface !!!
	if (hCamera) { gcDeleteCustomCamera(hCamera);  hCamera = NULL; }
	if (hRenderSrf) { oapiDestroySurface(hRenderSrf); hRenderSrf = NULL; }
}