# Yapper

Low-latency voice chat for people who won't shut up.

A simple way to talk with your friends. Secure, not bloated, and it just does what it should: voice, text chat, channels and screen sharing. Everything between client and server is encrypted.

**This project was mostly written by Claude. Issues are expected!**

## Server

The server reads `server.json` from its working directory. If it's missing, a default one is written:

```json
{
  "port": 5688,
  "name": "Yapper Server",
  "maxUsers": 32,
  "password": "",
  "channels": ["Lobby", "Gaming", "AFK"]
}
```

On first run it also creates `server.key`, the server's identity. Back it up. If it changes, every client refuses to connect until they remove the old entry from `%APPDATA%\Yapper\known_servers.txt`.

### Deploying (Docker on Debian)

The image contains only the server.

```sh
docker build -t yapper-server .
docker run -d --name yapper --restart unless-stopped \
  -p 5688:5688/tcp -p 5688:5688/udp \
  -v yapper-data:/data yapper-server
```

Edit the config in the volume, then `docker restart yapper`. Open both TCP and UDP 5688 in the firewall.

## Client (Windows)

Run `yapper_client.exe`, enter `host` or `host:port`, a name, and the password if there is one, then Connect.

- Click a channel to join it. You hear and chat with the people in your channel.
- Pick your mic and speakers under Settings.
- Share screen picks a monitor. Others click Watch next to your name to see it.

Test mode (no window, default devices, logs to `yapper_client.log` next to the exe):

```
yapper_client.exe host port username [password]
```

## Client (iPhone)

A SwiftUI client in `ios/`. It speaks the same protocol, so it talks to the same server and to Windows users. It does voice, text chat, channels, and watching screen shares. It can't share its own screen.

Build it on a Mac with Xcode 15 or newer:

```sh
brew install xcodegen
cd ios && xcodegen
open Yapper.xcodeproj
```

Xcode fetches libsodium and Opus as Swift packages. Pick your team under Signing & Capabilities, then run it on a phone.

For TestFlight, change the bundle id (`com.example.yapper`) to one you own, and raise `CURRENT_PROJECT_VERSION` in `ios/project.yml` for each upload. Then use Product > Archive and Distribute App. App Store Connect asks about encryption export compliance on the first upload, because the app uses libsodium.

Known servers are remembered in the app. If a server's key changes on purpose, delete and reinstall the app.

## License

MIT No Attribution (MIT-0): use it, change it, share it, no credit needed. See `LICENSE`. ImGui in `third_party/imgui` keeps its own MIT license.
