using System;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Real answers, trimmed to the fields the feeds read: GitHub's release list of the test repository (two test builds, as
    /// published 2026-10-28) and of the public repository (v0.1.84), and the BUILD-INFO.txt of those builds' zips (first lines).
    /// </summary>
    internal static class TestBuildSamples
    {
        public const string TestFeed = """
            [
             {
              "html_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983",
              "tag_name": "b189-ce6a983",
              "name": "Test build: 0.1.85 + 0.1.86 + next batch (b189, ce6a983)",
              "draft": false,
              "prerelease": true,
              "created_at": "2026-10-28T15:37:04Z",
              "published_at": "2026-10-28T22:51:00Z",
              "assets": [
               {
                "name": "EternalVR-alpha-0.1.84-ce6a983.zip",
                "size": 3711249,
                "digest": "sha256:d97a4ea87e0044021f7061847fa4c8a46c7396955643eba09f675c68c859da2a",
                "browser_download_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/download/b189-ce6a983/EternalVR-alpha-0.1.84-ce6a983.zip"
               },
               {
                "name": "EternalVR-alpha-0.1.84-ce6a983.zip.sha256",
                "size": 102,
                "digest": "sha256:74aa1b96aa025b9a9e4e9f16a1ddb702b6bee91ee991996fee4382df5a3b6a00",
                "browser_download_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/download/b189-ce6a983/EternalVR-alpha-0.1.84-ce6a983.zip.sha256"
               }
              ],
              "body": "Test build for 0.1.85 + 0.1.86 + the next batch (about 25 minutes, do what you can). It replaces b188: if you tried that one, only the NEW items below are new.\n\nReply in the Discord thread with the item number and OK / not OK / didn't try, plus an Export report."
             },
             {
              "html_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b188-ce70bbd",
              "tag_name": "b188-ce70bbd",
              "name": "Test build: 0.1.85 + 0.1.86 (b188, ce70bbd) - superseded by b189",
              "draft": false,
              "prerelease": true,
              "created_at": "2026-10-28T15:37:04Z",
              "published_at": "2026-10-28T15:37:23Z",
              "assets": [
               {
                "name": "EternalVR-alpha-0.1.84-ce70bbd.zip",
                "size": 3701598,
                "digest": "sha256:177a4e5470b4e1e281dd02e093a2b5bac8dd078b626e0aca7ca932a5b9e70283",
                "browser_download_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/download/b188-ce70bbd/EternalVR-alpha-0.1.84-ce70bbd.zip"
               },
               {
                "name": "EternalVR-alpha-0.1.84-ce70bbd.zip.sha256",
                "size": 102,
                "digest": "sha256:1cae1f9b83799c370458a9b586129a6de1bcf97086b43c3f9e0a822e2e4f041d",
                "browser_download_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/download/b188-ce70bbd/EternalVR-alpha-0.1.84-ce70bbd.zip.sha256"
               }
              ],
              "body": "**Superseded by [b189-ce6a983](https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983), which has everything here plus the next batch. Use that one.**\n\nTest build for 0.1.85 + 0.1.86 (about 20 minutes, do what you can)"
             }
            ]
            """;

        public const string MainFeed = """
            [
             {
              "html_url": "https://github.com/ArcadaLabs-Jason/EternalVR/releases/tag/v0.1.84",
              "tag_name": "v0.1.84",
              "name": "EternalVR v0.1.84 (alpha)",
              "draft": false,
              "prerelease": true,
              "published_at": "2026-10-26T00:47:38Z",
              "assets": [
               {
                "name": "EternalVR-alpha-0.1.84-8258af3.zip",
                "size": 3691625,
                "digest": "sha256:7ab7e29f610b50943ad138923e7709c25814d4d44915e4f1da2430cf3ffcac8c",
                "browser_download_url": "https://github.com/ArcadaLabs-Jason/EternalVR/releases/download/v0.1.84/EternalVR-alpha-0.1.84-8258af3.zip"
               },
               {
                "name": "EternalVR-alpha-0.1.84-8258af3.zip.sha256",
                "size": 102,
                "digest": "sha256:f633c3e140dedc188ad5cc1e6c42f16d9f48b0297cfeb4a237dd19be5d7ee7c2",
                "browser_download_url": "https://github.com/ArcadaLabs-Jason/EternalVR/releases/download/v0.1.84/EternalVR-alpha-0.1.84-8258af3.zip.sha256"
               }
              ],
              "body": "# EternalVR v0.1.84 (alpha)\n\nThe map takes the sticks the first time you open it, steadier HUD markers when you turn your head, Aim steadiness for the menu pointer, and the launcher shows your frame rate with Parallel Eye Rendering."
             }
            ]
            """;

        /// <summary>The public v0.1.84 release's BUILD-INFO.txt (as written, CRLF).</summary>
        public static readonly string Release084 = string.Join("\r\n",
            "EternalVR alpha 0.1.84",
            "commit: 8258af39bd4ad97821e11c4e0414c2051fe279e7",
            "commit time: 2026-10-25 18:29:07 -06:00",
            "packaged: 2026-10-25 18:31:40 -06:00",
            "layer: EternalVR.dll, CMake preset windows-msvc-release; OpenXR loader 1.1.63",
            "launcher: 0.1.84 (.NET Framework 4.8)",
            "");

        /// <summary>Test build b188's BUILD-INFO.txt.</summary>
        public static readonly string TestB188 = string.Join("\r\n",
            "EternalVR alpha 0.1.84",
            "commit: ce70bbd35931442d801aff503355f6b18d30563d",
            "commit time: 2026-10-26 20:48:08 -06:00",
            "packaged: 2026-10-26 22:22:40 -06:00",
            "layer: EternalVR.dll, CMake preset windows-msvc-release; OpenXR loader 1.1.63",
            "launcher: 0.1.84 (.NET Framework 4.8)",
            "");

        /// <summary>Test build b189's BUILD-INFO.txt.</summary>
        public static readonly string TestB189 = string.Join("\r\n",
            "EternalVR alpha 0.1.84",
            "commit: ce6a98336d6eadde4144d977ecd6f6e03fe38b24",
            "commit time: 2026-10-28 14:44:18 -06:00",
            "packaged: 2026-10-28 16:32:52 -06:00",
            "layer: EternalVR.dll, CMake preset windows-msvc-release; OpenXR loader 1.1.63",
            "launcher: 0.1.84 (.NET Framework 4.8)",
            "");

        public static readonly Version V084 = new Version(0, 1, 84, 0);
    }
}
