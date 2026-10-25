// Set the exe's icon and version info with resedit (pure JS, so this works when
// building on Linux without Wine, where electron-builder can't edit the exe itself).
const fs = require('fs'), path = require('path');
exports.default = async (ctx) => {
  if (ctx.electronPlatformName !== 'win32') return;
  const ResEdit = (await import('resedit')).default || (await import('resedit'));
  const name = ctx.packager.appInfo.productFilename;
  const exePath = path.join(ctx.appOutDir, name + '.exe');
  const exe = ResEdit.NtExecutable.from(fs.readFileSync(exePath), { ignoreCert: true });
  const res = ResEdit.NtExecutableResource.from(exe);
  const ico = ResEdit.Data.IconFile.from(fs.readFileSync(path.join(__dirname, 'build', 'icon.ico')));
  ResEdit.Resource.IconGroupEntry.replaceIconsForResource(res.entries, 1, 1033, ico.icons.map(i => i.data));
  const vi = ResEdit.Resource.VersionInfo.fromEntries(res.entries)[0];
  const v = ctx.packager.appInfo.version.split('.').map(Number);
  vi.setFileVersion(v[0], v[1], v[2], 0, 1033); vi.setProductVersion(v[0], v[1], v[2], 0, 1033);
  vi.setStringValues({ lang: 1033, codepage: 1200 }, { ProductName: 'Dragonstomper', FileDescription: 'Dragonstomper', CompanyName: 'The System', OriginalFilename: name + '.exe', InternalName: 'Dragonstomper' });
  vi.outputToResourceEntries(res.entries);
  res.outputResource(exe);
  fs.writeFileSync(exePath, Buffer.from(exe.generate()));
  console.log('  • set icon and version info on', path.basename(exePath));
};
