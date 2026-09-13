#include "ui/DefaultLayout.h"

// Captured from a real imgui.ini after arranging the panels by hand:
// Transport pinned across the top, Tracks down the left, Arrangement and
// Instrument tabbed together on the right, File and Timeline tabbed
// together along the bottom, and Piano Roll as the central node filling
// the rest. Only the windows SoundStudio actually creates are kept (no
// leftover demo/debug window entries).
const char* kDefaultImGuiLayoutIni =
R"imgui([Window][Piano Roll]
Pos=216,79
Size=687,597
Collapsed=0
DockId=0x00000008,0

[Window][Transport]
Pos=0,0
Size=1422,77
Collapsed=0
DockId=0x00000003,0

[Window][Tracks]
Pos=0,79
Size=214,597
Collapsed=0
DockId=0x00000004,0

[Window][Instrument]
Pos=905,79
Size=517,763
Collapsed=0
DockId=0x00000006,1

[Window][File]
Pos=0,678
Size=903,164
Collapsed=0
DockId=0x00000002,0

[Window][Arrangement]
Pos=905,79
Size=517,763
Collapsed=0
DockId=0x00000006,0

[Window][Timeline]
Pos=0,678
Size=903,164
Collapsed=0
DockId=0x00000002,1

[Docking][Data]
DockSpace         ID=0xF8498067 Window=0x1BBC0F80 Pos=0,0 Size=1422,842 Split=Y
  DockNode        ID=0x00000003 Parent=0xF8498067 SizeRef=1920,77 Selected=0x04C1370C
  DockNode        ID=0x00000007 Parent=0xF8498067 SizeRef=1920,930 Split=X
    DockNode      ID=0x00000005 Parent=0x00000007 SizeRef=1401,1009 Split=Y
      DockNode    ID=0x00000001 Parent=0x00000005 SizeRef=1920,843 Split=X
        DockNode  ID=0x00000004 Parent=0x00000001 SizeRef=214,764 Selected=0x96D9F442
        DockNode  ID=0x00000008 Parent=0x00000001 SizeRef=1185,764 CentralNode=1 Selected=0xC2D28E01
      DockNode    ID=0x00000002 Parent=0x00000005 SizeRef=1920,164 Selected=0x39CACFEF
    DockNode      ID=0x00000006 Parent=0x00000007 SizeRef=517,1009 Selected=0xCFD073F6
)imgui";
