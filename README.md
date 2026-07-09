# 3DS Spotify Token Setup

Nintendo 3DS homebrew utility that generates a Spotify `token.json` file on the SD card using Spotify Authorization Code with PKCE and QR codes.

The app is intended to make first-time Spotify Web API setup easier on a Nintendo 3DS. The user authorizes Spotify on a phone, then the 3DS scans the callback QR codes and writes the resulting token file to the SD card.

## Features

- Generates Spotify PKCE authorization data on the 3DS.
- Displays the Spotify authorization URL as a QR code.
- Uses a static GitHub Pages callback page.
- Supports multipart callback QR codes to make scanning reliable on 3DS hardware.
- Exchanges the authorization code for Spotify access and refresh tokens.
- Writes `token.json` to:

```text
sdmc:/config/spotify/token.json
```

## Token format

The generated file follows this structure:

```json
{
  "client_id": "CLIENT_ID",
  "access_token": "ACCESS_TOKEN",
  "refresh_token": "REFRESH_TOKEN",
  "expires_at": 0,
  "scope": "user-modify-playback-state user-read-playback-state user-read-currently-playing"
}
```

Do not commit a real `token.json` file. It contains private Spotify credentials.

## Requirements

- Nintendo 3DS with homebrew access.
- devkitPro / devkitARM.
- libctru.
- 3DS portlibs required by the Makefile:
  - curl
  - zlib
  - mbedTLS
- A Spotify Developer app.
- A deployed callback page, for example:

```text
https://lilcrespo.github.io/spotify-auth-qr-callback/
```

## Spotify Developer Dashboard setup

1. Open the Spotify Developer Dashboard.
2. Create a new app.
3. Copy the app's Client ID.
4. Add this Redirect URI exactly:

```text
https://lilcrespo.github.io/spotify-auth-qr-callback/
```

The Redirect URI must match the value compiled into the 3DS app exactly, including the trailing slash.

This app uses PKCE, so no client secret is needed on the 3DS.

## Building

From the project root:

```bash
make clean
make
```

The build produces:

```text
3ds-spotify-token-setup.3dsx
3ds-spotify-token-setup.smdh
```

The exact output name depends on the project folder name or the `TARGET` value in the Makefile.

## Installing

Copy the generated files to the SD card, for example:

```text
sdmc:/3ds/3ds-spotify-token-setup/3ds-spotify-token-setup.3dsx
sdmc:/3ds/3ds-spotify-token-setup/3ds-spotify-token-setup.smdh
```

Then launch the app from the Homebrew Launcher.

## Usage

1. Run the app on the 3DS.
2. Enter your Spotify Client ID.
3. Scan the QR code shown on the 3DS with your phone.
4. Authorize the app in Spotify.
5. The callback page will show one or more QR codes.
6. Scan each callback QR code with the 3DS when prompted.
7. The app will generate `sdmc:/config/spotify/token.json`.

Press `START` at any time to exit the app. Press `B` to cancel the current QR scan.

## Project structure

```text
include/       Public headers
source/        Application source code
libs/quirc/    QR decoder dependency
Makefile       devkitPro build file
```

## Dependencies included in the repository

This project vendors these small C libraries:

- `qrcodegen` for QR code generation.
- `quirc` for QR code decoding.

## Notes

- This project currently targets `.3dsx` homebrew builds only.
- CIA packaging and HOME Menu banner support are intentionally not included yet.
- The generated Spotify tokens are private. Never share or commit them.

## License

MIT License. See `LICENSE` for details.
