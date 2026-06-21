/***********************************************************************
DuneBox - Kinect for Windows v2 build prefix.

This header MUST be included before <ofMain.h> (and therefore before
<windows.h>) in every translation unit that pulls in the Kinect v2 backend.
It does two things:

  1. Force-enables the Kinect for Windows v2 backend on Windows (the OpenFrameworks
     Visual Studio project proved unreliable at injecting the compile flag into
     every translation unit, so we define it directly in source). macOS/Linux do
     not ship the ofxKinectForWindows2 addon, so the flag stays off there. Define
     DUNEBOX_NO_KINECT_V2 to opt out on Windows.

  2. Includes <winsock2.h> first. The v2 backend pulls in Kinect.h -> <windows.h>,
     which by default includes the legacy <winsock.h>. Including <winsock2.h>
     before <windows.h> defines _WINSOCKAPI_, which turns the later <winsock.h>
     inclusion into a no-op and avoids the winsock1/winsock2 "struct redefinition"
     errors. This is local to translation units that include this header; we do
     NOT define _WINSOCKAPI_ project-wide, which would break ofxKinect's
     libfreenect core.c (it relies on <winsock.h> for struct timeval).
***********************************************************************/
#pragma once

#if defined(_WIN32) && !defined(DUNEBOX_NO_KINECT_V2)
#  ifndef DUNEBOX_USE_KINECT_FOR_WINDOWS2
#    define DUNEBOX_USE_KINECT_FOR_WINDOWS2 1
#  endif
#endif

#ifdef DUNEBOX_USE_KINECT_FOR_WINDOWS2
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif
