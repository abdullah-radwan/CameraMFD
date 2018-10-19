#ifndef __CAMERAMFD_H
#define __CAMERAMFD_H

#include <map>

// MFD class
class CameraMFD: public MFD2 {
public:
	CameraMFD (DWORD w, DWORD h, VESSEL *vessel, UINT mfd);
	~CameraMFD ();

	static int MsgProc (UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam);

	char *ButtonLabel (int bt);
	int ButtonMenu (const MFDBUTTONMENU **menu);
	bool ConsumeButton (int bt, int event);
	bool ConsumeKeyBuffered (DWORD key);
	bool Update (oapi::Sketchpad *skp);

private:
	static char  buffer[256];         ///< Generic text manipulation buffer
	static double x, y, z, dz;
	static int fov;

	UINT          mfdIndex;           ///< MFD index number (from OAPI_MSG_MFD_OPENED message)
	oapi::Font    *font;              ///< Used font
	SURFHANDLE    hRenderSrf;         ///< 3D render target
	CAMERAHANDLE  hCamera;            ///< Custom camera handle used to render views into surfaces and textures

	void SetupCustomCamera ();
};

#endif // !__CAMERAMFD_H