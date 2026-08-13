# ApkVulFuzz
Fuzzing APK as binaries to hit vulnerabilities

## Publications:

Even-Mendoza, K., Dakhama, A., & Berger, H. (Accepted/In press). ApkFuzz: Search-Based Fuzzing for Android APK Vulnerability. Lecture Notes in Computer Science, 6.

```
@article{f925a64a9a1e4951a099d266af61659a,
title = "ApkFuzz: Search-Based Fuzzing for Android APK Vulnerability",
abstract = "While most work focuses on executable code, AndroidManifest.xml, which is the primary entry point for application metadata and permission logic, is often overlooked, making it a high-trust, low-scrutiny attack surface. We present ApkFuzz, an AFL-based fuzzer with bit-level mutations targeting the encoded region of AndroidManifest.xml in the APK file. We have identified 9 issues that cause crashes and enable denial-of- service (DoS) attacks in core tools, including Google{\textquoteright}s apksigner and the widely used research tool DroidBot.",
keywords = "Fuzzing, APK, Android, vulnerability detection",
author = "Karine Even-Mendoza and Aidan Dakhama and Harel Berger",
year = "2026",
month = apr,
day = "30",
language = "English",
pages = "6",
journal = "Lecture Notes in Computer Science",
issn = "1611-3349",
publisher = "Springer",
note = "18TH INTERNATIONAL SYMPOSIUM ON SEARCH-BASED SOFTWARE ENGINEERING (SSBSE), SSBSE ; Conference date: 06-07-2026 Through 06-07-2026",
url = "https://conf.researchr.org/venue/ssbse-2026/fse-2026-venue",
}
```

Even-Mendoza, K., Dakhama, A., & Berger, H. (2026). Artifact of ApkFuzz: Search-Based Fuzzing for Android APK Vulnerability Discovery (SSBSE2026(v1.1)). Zenodo. https://doi.org/10.5281/zenodo.19476523

```
@misc{even_mendoza_2026_19476523,
  author       = {Even-Mendoza, Karine and
                  Dakhama, Aidan and
                  Berger, Harel},
  title        = {Artifact of ApkFuzz: Search-Based Fuzzing for
                   Android APK Vulnerability Discovery
                  },
  month        = apr,
  year         = 2026,
  publisher    = {Zenodo},
  version      = {SSBSE2026(v1.1)},
  doi          = {10.5281/zenodo.19476523},
  url          = {https://doi.org/10.5281/zenodo.19476523},
}
```


## Installing a Simulator of APK

**1.** Install pre-requirements
```
sudo apt update
sudo apt install -y python3 python3-pip openjdk-17-jdk unzip wget
sudo apt install -y libxml2-utils
sudo apt install -y apksigner adb
python3 -m pip install --upgrade pip
python3 -m pip install --upgrade setuptools
python3 -m pip install --upgrade pip setuptools wheel networkx Pillow
python3 -m pip install --user "androguard==3.4.0a1"
```
  
**2.** Setup Android SDK root environment
```
mkdir -p "$HOME/android-sdk/cmdline-tools"
export ANDROID_SDK_ROOT="$HOME/android-sdk"
export ANDROID_HOME="$ANDROID_SDK_ROOT"

cd /tmp
wget https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip -O cmdline-tools.zip
unzip -q cmdline-tools.zip -d /tmp/cmdline-tools-unpacked

mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools/latest"
mv /tmp/cmdline-tools-unpacked/cmdline-tools/* "$ANDROID_SDK_ROOT/cmdline-tools/latest/"

export PATH="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/emulator:$PATH"
export PATH="$ANDROID_SDK_ROOT/emulator:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$PATH"

## Optional if you want to install it for good
cat >> ~/.bashrc <<'EOF'
export ANDROID_SDK_ROOT="$HOME/android-sdk"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export PATH="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/emulator:$PATH"
export PATH="$ANDROID_SDK_ROOT/emulator:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$PATH"
EOF
source ~/.bashrc
## End of optional part

sdkmanager --sdk_root="$ANDROID_SDK_ROOT" --licenses
```

The install SDK for X86:
```
sdkmanager --sdk_root="$ANDROID_SDK_ROOT" \
  "platform-tools" \
  "emulator" \
  "platforms;android-34" \
  "system-images;android-34;google_apis;x86_64"
```
Or for ARM:

```
sdkmanager --sdk_root="$ANDROID_SDK_ROOT" \
  "platform-tools" \
  "emulator" \
  "platforms;android-34" \
  "system-images;android-34;google_apis;arm64-v8a"
```

and then
```
sdkmanager "build-tools;34.0.0"
```

**3.** Check installation:

```
adb version
sdkmanager --list | head
avdmanager list target | head
apkanalyzer --help | head
```

Which then returns something like this:
```
Android Debug Bridge version 1.0.41
Version 37.0.0-14910828
Installed as /users/anon/android-sdk/platform-tools/adb
Running on Linux 5.15.0-168-generic (x86_64)
[=======================================] 100% Computing updates...             
Installed packages:
  Path                                        | Version | Description                                | Location                                   
  -------                                     | ------- | -------                                    | -------                                    
  build-tools;34.0.0                          | 34.0.0  | Android SDK Build-Tools 34                 | build-tools/34.0.0                         
  emulator                                    | 36.4.9  | Android Emulator                           | emulator                                   
  platform-tools                              | 37.0.0  | Android SDK Platform-Tools                 | platform-tools                             
  platforms;android-34                        | 3       | Android SDK Platform 34                    | platforms/android-34                       
  system-images;android-34;google_apis;x86_64 | 14      | Google APIs Intel x86_64 Atom System Image | system-images/android-34/google_apis/x86_64

[=======================================] 100% Fetch remote repository...       
Available Android targets:
----------
id: 1 or "android-34"
     Name: Android API 34, extension level 7
     Type: Platform
     API level: 34
     Revision: 3
Verb must be one of: 

Usage:
apkanalyzer [global options] <subject> <verb> [options] <apk> [<apk2>]

Option            Description                         
------            -----------                         
--human-readable  Print sizes in human readable format
```

**4.** Install the emulator + start:

Run this:
```
avdmanager create avd -n test34 -k "system-images;android-34;google_apis;x86_64"
```

Check if it is created:
```
emulator -list-avds
```

And start the emulator:
```
emulator -avd test34 -no-window -no-audio -no-boot-anim -no-metrics -no-snapshot > /tmp/emulator.log 2>&1 &
adb wait-for-device
until [ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = "1" ]; do sleep 2; done
adb devices -l
```

**5.** Install the simulator:

```
git clone https://github.com/honeynet/droidbot.git
cd droidbot

python3 -m pip uninstall -y androguard
python3 -m pip install "androguard>=3.4.0a1,<4"

python3 -m pip install -e .

adb -s emulator-5554 install droidbot/resources/droidbotApp.apk
adb -s emulator-5554 shell ime enable io.github.ylimit.droidbotapp/.DroidBotIME
```
You can read further on how to use this emulator: [https://github.com/honeynet/droidbot]   

Then you can test with a valid APK:
```
python3 start.py -a ../ApkVulFuzz/Evaluation-SSBSE-2026/seeds/F-Droid.apk -d emulator-5554 -policy monkey -interval 0 -timeout 10 -o results/fdroid_run_10s -is_emulator -grant_perm
```

### Troubleshooting

Number 1 problem and the root cause of 99% of the issues is not properly running "headless" environment. Ask LLM to solve it with this hint.

Some other issues are listed below.

#### Problem 2:
If no emulator up:
```
user@node0:~/droidbot$ adb devices
List of devices attached

user@node0:~/droidbot$
```
to fix it, run:
```
sudo apt update
sudo apt install -y libpulse0
sudo gpasswd -a $USER kvm
sudo usermod -aG kvm $USER
newgrp kvm
emulator -avd test34 -no-window -no-audio -no-metrics &
```

#### Problem 3:

If you can run the emulator, but get this error:
```
WARNING:DroidBotAppConn:Restarting droidbot app 51059 Traceback (most recent call last): File "/users/user/droidbot/droidbot/adapter/droidbot_app.py", line 140, in listen_messages _, _, message_len = self.read_head() File "/users/user/droidbot/droidbot/adapter/droidbot_app.py", line 131, in read_head header = self.sock_read(DROIDBOT_APP_PACKET_HEAD_LEN) File "/users/user/droidbot/droidbot/adapter/droidbot_app.py", line 122, in sock_read raise EOF()
```

Then run:
```
adb shell getprop sys.boot_completed
adb kill-server
adb start-server
adb install /users/anon/droidbot/droidbot/resources/droidbotApp.apk
adb shell settings put secure enabled_accessibility_services io.github.ylimit.droidbotapp/io.github.privacystreams.accessibility.PSAccessibilityService
adb shell settings put secure accessibility_enabled 1
pkill -f start.py
```


## General Ways to Check APK

Partial list, we need to add the rest:
```
apksigner verify  F-Droid_f35baa5a.apk
zipinfo F-Droid_f35baa5a.apk
zipinfo -v F-Droid_f35baa5a.apk
unzip F-Droid_fff8d915.apk
androguard axml AndroidManifest.xml
```

## Blackbox fuzzing

Put one or more APKs in a seed folder, then run:
```
./scripts/search-blackbox.sh --short seeds > all.log 2>&1
```
The script fuzzes each original APK in that folder. When a mutation chain breaks, it moves to the next original APK instead of stopping the whole run.

The mutators dynamically locate `AndroidManifest.xml` inside each APK instead of using hardcoded offsets. This is the same idea as checking the APK with `zipinfo`: the code reads the ZIP metadata, finds the manifest entry, computes the manifest byte range, and mutates bytes inside that range.

## Greybox fuzzing

See instructions in `src` folder.

## Test Corrupted APK

First, try this command:
```
apkanalyzer apk summary F-Droid.apk 
```

Run this command to test if the apk is behaving oddly:
```
user@nmscmo008175 droidbot % python3 start.py -a ../F-Droid.apk -d emulator-5554 -policy bfs_greedy -count 100 -interval 2 -timeout 20 -o results/fdroid_run -is_emulator -grant_perm
```
