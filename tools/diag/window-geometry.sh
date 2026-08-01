#!/bin/bash
# Print the CURRENT geometry of running windows, without closing them.
#
# usage: window-geometry.sh [title-substring]      (default: TTCut)
#
# Asks KWin over DBus, so it needs no xdotool/wmctrl and works on Wayland.
# KDE-specific; on other desktops it prints nothing.
#
# What it reports is the window as it is RIGHT NOW - maximised windows show
# the screen size. That is a different quantity from what the application
# stores in its settings file, which is the *normal* (un-maximised) geometry.
# Both are correct; do not compare them directly.
#
# When comparing two window states, change ONE thing at a time. Comparing a
# small unmaximised window against a large maximised one varies size and state
# together and supports no conclusion about either - a mistake made with this
# very script on 2026-08-01.
MATCH="${1:-TTCut}"
JS=$(mktemp /tmp/wingeom-XXXX.js)
cat > "$JS" <<JSEOF
var l = workspace.windowList ? workspace.windowList() : workspace.clientList();
for (var i = 0; i < l.length; i++) {
    var c = l[i];
    if (c.caption && c.caption.indexOf("$MATCH") !== -1)
        print("WINGEOM " + Math.round(c.frameGeometry.width) + "x" +
              Math.round(c.frameGeometry.height) + " at " +
              Math.round(c.frameGeometry.x) + "," + Math.round(c.frameGeometry.y) +
              "  [" + c.caption + "]");
}
JSEOF
ID=$(qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$JS")
qdbus org.kde.KWin /Scripting/Script$ID org.kde.kwin.Script.run >/dev/null 2>&1
sleep 0.5
journalctl --user --since "-5s" 2>/dev/null | grep -oP 'WINGEOM \K.*' | sort -u
qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$JS" >/dev/null 2>&1
rm -f "$JS"
