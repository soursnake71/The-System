// What the game page may ask of the desktop app: only to quit.
const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('desktop', { quit: () => ipcRenderer.send('quit') });
