#ifndef __CAMERAMFD_H
#define __CAMERAMFD_H

#include <vector>
#include <map>

struct Info {
	struct camera { VECTOR3 pos, dir, rot; int fov; };
	std::vector<camera> camVector;
	int cam, adj;
};

// MFD class
class CameraMFD: public MFD2 {
public:
	static int MsgProc(UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam);
	CameraMFD (DWORD w, DWORD h, VESSEL *vessel, UINT mfd);
	void ReadStatus(FILEHANDLE scn);

	bool Update(oapi::Sketchpad *skp);
	char *ButtonLabel (int bt);
	int ButtonMenu (const MFDBUTTONMENU **menu) const;
	bool ConsumeButton (int bt, int event);
	bool ConsumeKeyBuffered (DWORD key);
	
	void WriteStatus(FILEHANDLE scn) const;
	~CameraMFD();

private:
	static std::map<int, Info> info;

	UINT          mfdIndex;           ///< MFD index number (from OAPI_MSG_MFD_OPENED message)
	oapi::Font    *font;              ///< Used font
	SURFHANDLE    hRenderSrf;         ///< 3D render target
	CAMERAHANDLE  hCamera;            ///< Custom camera handle used to render views into surfaces and textures

	void SetupCustomCamera();
};

#endif // !__CAMERAMFD_H