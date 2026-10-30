# vpet for Android

The Android app is the same SDL frontend as the PC build
(`platform/sdl/main.cpp`), built through SDL2's Android port. On Android
the screen sits inside a drawn handheld shell (`platform/shell/`) with
three touch buttons to its right; each finger is tracked on its own, so
two thumbs can hold A+C for settings.

Everything in the repo's `packs/` folder is bundled into the APK at build
time (your local packs too, so an APK with personal packs is for your own
phone only, not for sharing), with a generated `packs/index.txt` listing
them, since files inside an APK can't be listed at runtime. Saves go to the
app's private storage.

## What you need

- JDK 17 (for example `winget install Microsoft.OpenJDK.17`)
- Android SDK command-line tools, then:
  `sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264" "cmake;3.22.1"`
- SDL2 source, version 2.30.x: <https://github.com/libsdl-org/SDL/releases>

Point the build at the SDK and SDL in `platform/android/local.properties`
(not committed):

```
sdk.dir=C\:/Android/sdk
sdl2.dir=C\:/Android/src/SDL2-2.30.9
```

## Build

From `platform/android/` with `JAVA_HOME` set to the JDK:

```
gradlew assembleRelease
```

The APK is `app/build/outputs/apk/release/app-release.apk`. It is signed
with the local debug key so it installs directly: copy it to the phone and
open it (allow installing from unknown sources when asked), or with a USB
cable and developer mode on, `adb install -r app-release.apk`. Updates
built on the same PC install over the old version and keep the save.

The Java files under `app/src/main/java/org/libsdl/app/` come from SDL2
2.30.9 (zlib license) unchanged; keep them matched to the SDL version in
`sdl2.dir`.
