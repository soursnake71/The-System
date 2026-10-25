// Dragonstomper desktop: a fullscreen window that serves the game from inside the app
// (app://game/...), because browsers won't load ES modules straight from local files.
const { app, BrowserWindow, protocol, net, Menu, ipcMain } = require('electron');
const path = require('path');
const { pathToFileURL } = require('url');

protocol.registerSchemesAsPrivileged([
  { scheme: 'app', privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true, corsEnabled: true } },
]);
// the game is GPU-heavy: don't let Chromium fall back to software on older drivers
app.commandLine.appendSwitch('ignore-gpu-blocklist');
app.commandLine.appendSwitch('enable-gpu-rasterization');

const ROOT = path.join(__dirname, 'game');

function createWindow() {
  const win = new BrowserWindow({
    width: 1600, height: 900, fullscreen: true, backgroundColor: '#07080c', title: 'Dragonstomper',
    icon: path.join(__dirname, 'game', 'icon-512.png'), autoHideMenuBar: true, show: false,
    webPreferences: { preload: path.join(__dirname, 'preload.js'), backgroundThrottling: false, contextIsolation: true, sandbox: true },
  });
  win.once('ready-to-show', () => win.show());
  // F11 toggles fullscreen
  win.webContents.on('before-input-event', (e, input) => {
    if (input.type === 'keyDown' && input.key === 'F11') { win.setFullScreen(!win.isFullScreen()); e.preventDefault(); }
  });
  // links (credits) open in the user's browser, not inside the game window
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https?:/.test(url)) require('electron').shell.openExternal(url); return { action: 'deny' }; });
  win.loadURL('app://game/index.html');
}

app.whenReady().then(() => {
  protocol.handle('app', (req) => {
    let p = decodeURIComponent(new URL(req.url).pathname);
    if (p === '/' || p === '') p = '/index.html';
    const file = path.normalize(path.join(ROOT, p));
    if (!file.startsWith(ROOT)) return new Response('Forbidden', { status: 403 });
    return net.fetch(pathToFileURL(file).toString());
  });
  Menu.setApplicationMenu(null);
  ipcMain.on('quit', () => app.quit());
  createWindow();
});
app.on('window-all-closed', () => app.quit());
