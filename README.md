# Arcdps-Uploader
This is an extension for Arcdps that allows you to upload EVTC combat logs *in-game*.

![Image of the Uploader](https://imgur.com/a/Cw75kax.png)

## Usage
Grab the latest [release](https://github.com/datatobridge/arcdps-uploader/releases). Unzip and copy `d3d9_uploader.dll` to same directory as **Arcdps** (usually something like `C:\Program Files\Guild Wars 2\bin64`).

Use *Alt-Shift-U* to bring the uploader window up.

## Changelog
**0.9:**
- Automatically uploads logs when not in combat
- Discord Webhook integration
  - Filter by success, log type, or players present
- Removed (unmaintained) built-in parser

**0.8:**
- Cache all parsed logs to prevent any refresh delay for users with 500+ logs
- Add Wing 7 Boss Names and ID's
- Improve parsing accuracy (dps should match Elite Insights)

## ToS Compliance
This extension has minimal interaction with GW2 (basically it displays a window), and is essentially a QoL upgrade over uploading the logs yourself. It provides no inherent advantage over other players. As such, I believe it to be ToS compliant.

If you have any doubts, refer to [Arenanet's policy on Third-Party Programs](https://en-forum.guildwars2.com/discussion/65547/policy-third-party-programs).

## Support
Please open an issue and leave a detailed description of your problem, feature request, etc.

*Thanks to Arc/Delta for writing and supporting Arcdps, and the Elite Insights team for their excellent parser*
