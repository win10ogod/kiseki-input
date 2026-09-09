Download the installer matching your operating system and CPU from **Assets**:

- **Windows x64:** `.exe` installer; `.zip` for a portable installation.
- **macOS 14+:** `Darwin-arm64.pkg` for Apple Silicon or `Darwin-x86_64.pkg` for Intel. Archives are also available.
- **Linux:** `.deb` for Ubuntu 22.04+ / compatible Debian systems; `.tar.gz` for a custom prefix. Choose `x86_64` or `aarch64`.

Every package has a matching `.sha256` file. Windows packages include the optional IbInputSimulator DLL; device-specific driver prerequisites remain separate.

After installation, open a new terminal and run `kiseki --version`. To begin CUA operation, run `kiseki background cua setup`: it installs a missing official Cua Driver and automatically checks for updates once a day at workflow startup. macOS requires the normal Accessibility and Screen Recording grants.

Read the [installation guide](https://github.com/win10ogod/kiseki-input/blob/master/docs/install.md) or open `share/kiseki/install.html` in your installation for step-by-step instructions, agent skill locations, and troubleshooting.
