# Orchid Client

A modified [Armagetron Advanced](https://gitlab.com/armagetronad/armagetronad) client for Linux and Windows.

## Windows

1. Download [orchid-client-windows.zip](https://github.com/orchidoxion2/Orchid-Client-an-armagetron-client/releases/latest/download/orchid-client-windows.zip).
2. Right-click the zip and choose **Extract All**.
3. Open the extracted `Orchid Client` folder and double-click **Orchid Client.exe**.

If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**.
Your settings are saved in `%APPDATA%\Orchid Client`.

## Linux

1. Download [orchid-client-linux-x86_64.tar.gz](https://github.com/orchidoxion2/Orchid-Client-an-armagetron-client/releases/latest/download/orchid-client-linux-x86_64.tar.gz).
2. Unpack it:
   ```bash
   tar -xzf orchid-client-linux-x86_64.tar.gz
   ```
3. Start the game:
   ```bash
   ./orchid-client/AppRun
   ```
4. Optional: add Orchid Client to your app menu, so you can search for it with the Windows key:
   ```bash
   ./orchid-client/install-launcher.sh
   ```

## Source code

The full source code is in the [`source`](https://github.com/orchidoxion2/Orchid-Client-an-armagetron-client/tree/source) branch. Every [release](https://github.com/orchidoxion2/Orchid-Client-an-armagetron-client/releases) also has a **Source code** download with the exact code it was built from.

## License

Orchid Client is based on Armagetron Advanced and is licensed under the GNU General Public License, version 2 or later. See [COPYING](COPYING).
