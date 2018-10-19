#define STRICT
#define ORBITER_MODULE
#include <windows.h>

#include "orbitersdk.h"
#include "gcAPI.h"
#include "CameraMFD.h"
#include "Sketchpad2.h"

// ==============================================================
// API interface

int g_MFDmode;  ///< identifier for new MFD mode

DLLCLBK void InitModule (HINSTANCE hDLL)
{
	static char *name = "Camera MFD";    // MFD mode name
	MFDMODESPECEX spec;
	spec.name = name;
	spec.key = OAPI_KEY_C;               // MFD mode selection key
	spec.context = NULL;
	spec.msgproc = CameraMFD::MsgProc;   // MFD mode callback function

	// Register the new MFD mode with Orbiter
	g_MFDmode = oapiRegisterMFDMode(spec);
}

DLLCLBK void ExitModule (HINSTANCE hDLL)
{
	// Unregister the custom MFD mode when the module is unloaded
	oapiUnregisterMFDMode(g_MFDmode);
}

// ==============================================================
// MFD class implementation

// --- Class Statics ---
char CameraMFD::buffer[256] = ""; // Initialize static buffer
double CameraMFD::x, CameraMFD::y, CameraMFD::z = 0;
double CameraMFD::dz = -1;
int CameraMFD::fov = 40;

// MFD message parser
int CameraMFD::MsgProc (UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam)
{
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
	: MFD2(w, h, vessel), mfdIndex(mfd % MAXMFD),
	  hRenderSrf(NULL),
	  hCamera   (NULL)
{
	font = oapiCreateFont(w/20, true, "Sans", FONT_NORMAL);

	if (gcInitialize())
	{

		// Create 3D render target
		hRenderSrf = oapiCreateSurfaceEx(W, H, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET |
		                                                       OAPISURFACE_RENDER3D | OAPISURFACE_NOMIPMAPS);
		// Clear the surface
		oapiClearSurface(hRenderSrf);

		// Actual rendering of the camera view into hRenderSrf will occur when the client
		// is ready for it and a lag of a few frames may occur depending on
		// graphics/performance options.
		// Update will continue until the camera is turned off via gcCustomCameraOnOff()
		// or deleted via gcDeleteCustomCamera()
		// Camera orientation can be changed by calling this function again with an existing
		// camera handle instead of NULL.
		//
		SetupCustomCamera();
	}
}

// --- Destructor ---
CameraMFD::~CameraMFD ()
{
	oapiReleaseFont(font);
	// Attention, Always delete the camera before the surface !!!
	if (hCamera)    { gcDeleteCustomCamera(hCamera);  hCamera = NULL;    }
	if (hRenderSrf) { oapiDestroySurface(hRenderSrf); hRenderSrf = NULL; }
}

// Return button labels
char *CameraMFD::ButtonLabel (int bt)
{
	// The labels for the buttons used by our MFD mode
	static char *label[] = { "RIT", "LFT", "FWD", "BCK", "UP", "DWN", "VW+", "VW-", "DIR", "RST"};
	return (bt < ARRAYSIZE(label)) ? label[bt] : NULL;
}

// Return button menus
int CameraMFD::ButtonMenu (const MFDBUTTONMENU **menu)
{
	// The menu descriptions for the buttons
	static const MFDBUTTONMENU mnu[] = {
		{ "Move camera right"    , 0   , 'R' },
		{ "Move camera left"     , 0   , 'L' },
		{ "Move camera forward"  , 0   , 'F' },
		{ "Move camera backwards", 0   , 'B' },
		{ "Move camera up"       , 0   , 'U' },
		{ "Move camera down"     , 0   , 'D' },
		{ "Field of view", "Increment" , 'Z' },
		{ "Field of view", "Decrement" , 'X' },
		{ "Set camera direction" , 0   , 'I' },
		{ "Reset coordinates"    , 0   , 'S' }
	};
	if (menu) { *menu = mnu; }
	return ARRAYSIZE(mnu); // return the number of buttons used
}

// Button handler
bool  CameraMFD::ConsumeButton (int bt, int event)
{
	if (event & PANEL_MOUSE_LBDOWN)
	{
		static const DWORD btkey[] = {
			OAPI_KEY_R, OAPI_KEY_L, // X
			OAPI_KEY_F, OAPI_KEY_B, // Y
			OAPI_KEY_U, OAPI_KEY_D, // Z
			OAPI_KEY_Z, OAPI_KEY_X,  // FoV
			OAPI_KEY_I, // Direction
			OAPI_KEY_S // Reset
		};
		if (bt < ARRAYSIZE(btkey) && btkey[bt] != NULL) {
			return ConsumeKeyBuffered(btkey[bt]);
		}
	}
	return false;
}

// Key handler
bool CameraMFD::ConsumeKeyBuffered (DWORD key)
{

	switch (key)
	{
	case OAPI_KEY_R:
		x += 0.5;
		SetupCustomCamera();
		break;
	case OAPI_KEY_L:
		x -= 0.5;
		SetupCustomCamera();
		break;
	case OAPI_KEY_F:
		y += 0.5;
		SetupCustomCamera();
		break;
	case OAPI_KEY_B:
		y -= 0.5;
		SetupCustomCamera();
		break;
	case OAPI_KEY_U:
		z += 0.5;
		SetupCustomCamera();
		break;
	case OAPI_KEY_D:
		z -= 0.5;
		SetupCustomCamera();
		break;
	case OAPI_KEY_Z:
		if (fov < 60) {
			fov += 10;
			SetupCustomCamera();
		}
		break;
	case OAPI_KEY_X:
		if (fov > 10) {
			fov -= 10;
			SetupCustomCamera();
		}
		break;
	case OAPI_KEY_I:
		dz = -dz;
		SetupCustomCamera();
		break;
	case OAPI_KEY_S:
		x = 0;
		y = 0;
		z = 0;
		SetupCustomCamera();
		break;
	default:
		return false;
	}

	// InvalidateButtons(); No dynamic buttons; so not needed!
	InvalidateDisplay();
	return true;
}

// Repaint the MFD page
bool CameraMFD::Update (oapi::Sketchpad *skp)
{
	// Helper for static texts
	auto SKPTEXT = [skp](int x, int y, const char *str) { skp->Text(x, y, str, strlen(str)); };
	#define SKPTEXT(x,y,str) (skp->Text(x, y, str, sizeof(str)-1))

	if (hRenderSrf && gcSketchpadVersion(skp) == 2)
	{
		Sketchpad2 *skp2 = (Sketchpad2 *)skp;

		// Blit the camera view into the sketchpad.
		RECT sr = { 0, 0, LONG(W), LONG(H) };
		skp2->CopyRect(hRenderSrf, &sr, 0, 0);

	}

	// Draw an MFD title
	Title(skp, "Camera MFD");

	skp->Rectangle(0, 0, W, H);

	skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::BASELINE);
	if (!hCamera) SKPTEXT(W / 2, H / 2, "Custom camera interface disabled");
	else if (!gcEnabled()) SKPTEXT(W / 2, H / 2, "No Graphics Client");
	else if (gcSketchpadVersion(skp) != 2) SKPTEXT(W / 2, H / 2, "Sketchpad Not in DirectX Mode");

	return true;
}

// Apply changes to the (current) custom camera object
void CameraMFD::SetupCustomCamera ()
{

	hCamera = gcSetupCustomCamera( hCamera, pV->GetHandle(),
		VECTOR3{ x,y,z }, VECTOR3{ 0,0,dz }, VECTOR3{ 0,1,0 }, fov * RAD, hRenderSrf, 0xFF );

}