// Windows 7 taskbar "Cascade windows" / "Show windows stacked" / "Show windows side by side".
// MODE is replaced by the launcher script.
const mode = "MODE";
const area = workspace.clientArea(KWin.MaximizeArea, workspace.activeScreen, workspace.currentDesktop);
const wins = workspace.stackingOrder.filter(w =>
    w.normalWindow && !w.minimized && !w.skipTaskbar && w.output === workspace.activeScreen &&
    (w.onAllDesktops || w.desktops.indexOf(workspace.currentDesktop) >= 0));
const n = wins.length;
wins.forEach((w, i) => {
    w.setMaximize(false, false);
    if (w.fullScreen) { w.fullScreen = false; }
    let g;
    if (mode === "cascade") {
        const step = 26;                       // one title bar
        const width = Math.round(area.width * 0.6), height = Math.round(area.height * 0.6);
        const k = i % Math.max(1, Math.floor(Math.min(area.height - height, area.width - width) / step) + 1);
        g = {x: area.x + k * step, y: area.y + k * step, width: width, height: height};
    } else if (mode === "stack") {             // horizontal strips, one above the other
        const h = Math.floor(area.height / n);
        g = {x: area.x, y: area.y + i * h, width: area.width, height: h};
    } else {                                   // side by side columns
        const wd = Math.floor(area.width / n);
        g = {x: area.x + i * wd, y: area.y, width: wd, height: area.height};
    }
    w.frameGeometry = g;
});
