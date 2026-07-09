# 3DS Spotify Token Setup

Nintendo 3DS homebrew utility that generates a Spotify `token.json` file on the SD card using Spotify Authorization Code with PKCE and QR codes.

The app is intended to make first-time Spotify Web API setup easier on a Nintendo 3DS. The user authorizes Spotify on a phone, then the 3DS scans the callback QR codes and writes the resulting token file to the SD card.

Writes `token.json` to:

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

## Requirements

- Nintendo 3DS with homebrew access.
- A Spotify Developer app.

## Spotify Developer Dashboard setup

1. Open the Spotify Developer Dashboard.
2. Create a new app.
3. Copy the app's Client ID.
4. Add this Redirect URI exactly:

```text
https://lilcrespo.github.io/spotify-auth-qr-callback/
```

This app uses PKCE, so no client secret is needed on the 3DS.

## Building

### Requirements

You need a working devkitPro Nintendo 3DS development environment.

Required tools and libraries for building the `.3dsx`:

- devkitARM
- libctru
- 3ds-curl
- 3ds-mbedtls
- make

To build the `.cia`, you also need

- makerom
- bannertool

### Build

#### Build `.3dsx`

From the project root:

```bash
make clean
make
```

#### Build `.cia`

From the project root:

```bash
make clean
make cia
```

## Installing

### Install `.3dsx`

Copy the generated files to the SD card, for example:

```text
sdmc:/3ds/3ds-spotify-api-token-setup.3dsx
```

Then launch the app from the Homebrew Launcher.

### Install `.cia`

Install the `.cia` with `FBI`. Then launch the app from the home menu.

## Usage

1. Run the app on the 3DS.
2. Enter your Spotify Client ID.
3. Scan the QR code shown on the 3DS with your phone.
4. Authorize the app in Spotify.
5. The callback page will show one or more QR codes.
6. Scan each callback QR code with the 3DS when prompted.
7. The app will generate `sdmc:/config/spotify/token.json`.

## License

MIT License. See `LICENSE` for details.
