// Windows 7 window keys
function active() {
    var w = workspace.activeWindow;
    return (w && w.normalWindow) ? w : null;
}
function isMaximized(w) {
    var a = workspace.clientArea(KWin.MaximizeArea, w);
    return Math.abs(w.frameGeometry.width - a.width) < 2 && Math.abs(w.frameGeometry.height - a.height) < 2;
}
registerShortcut("Win7 Maximize", "Windows 7: розгорнути вікно", "Meta+Up", function () {
    var w = active();
    if (w && w.maximizable) w.setMaximize(true, true);
});
registerShortcut("Win7 Restore or Minimize", "Windows 7: відновити або згорнути вікно", "Meta+Down", function () {
    var w = active();
    if (!w) return;
    if (isMaximized(w) && w.maximizable) w.setMaximize(false, false);
    else if (w.minimizable) w.minimized = true;
});
registerShortcut("Win7 Maximize Vertically", "Windows 7: розгорнути вікно по вертикалі", "Meta+Shift+Up", function () {
    var w = active();
    if (w && w.maximizable) w.setMaximize(true, false);
});
// Aero Shake: minimise every other window; the second press brings them back
var shaken = [];
registerShortcut("Win7 Shake", "Windows 7: згорнути всі вікна, крім активного", "Meta+Home", function () {
    var w = active();
    if (shaken.length) {
        for (var i = 0; i < shaken.length; i++) shaken[i].minimized = false;
        shaken = [];
        if (w) workspace.activeWindow = w;
        return;
    }
    var all = workspace.windowList();
    for (var i = 0; i < all.length; i++) {
        var o = all[i];
        if (o !== w && o.normalWindow && !o.minimized && o.minimizable && o.isOnCurrentDesktop !== false) {
            o.minimized = true; shaken.push(o);
        }
    }
});
