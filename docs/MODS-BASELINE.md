# Mod baseline — set 2026-08-10 (the clean-slate co-op game)

**60 mods, fingerprint `2d49eaad` (60).** The handshake's `[mods]` line
must read exactly `[mods] match: 60 mods, fingerprint 2d49eaad` on BOTH
clients — anything else means the baselines have drifted apart, and every
world-difference symptom that follows is mod drift, not netcode.

Cut down from the earlier 103 on 2026-08-10 (43 turned off, 0 added) after
a mismatched-mods session burned an evening. Notable: Reactive World,
Slopeless, citizens, More Recruits, Guards for the Hub, 256 Squad Limit,
TownOwnership and Towns Root are OFF (the AI/population/ownership heavies).
Still ON and relevant to how movement looks: **AnimationOverhaul** and
**WASDCombatPlugin** — if NPC motion artifacts survive the netcode fixes,
an A/B without AnimationOverhaul is the cheap next check.

Verify current state: `tr -d '\r' < "<Kenshi>/data/mods.cfg" | wc -l` and
compare against the list below (ORDER MATTERS — later mods override earlier).

```
blot ration packs.mod
WASDCombatPlugin.mod
Strength Training Equipment.mod
ShriekingBandit_Expansion.mod
RecruitPrisoners.mod
Nice Map [Zones + Zone names + Roads].mod
New Weapons Dissemination Mod.mod
KenshiCoop.mod
ImpalerArmor.mod
Hiver_Expansion.mod
GusokuArmorV1.0.mod
Fixing Clipping Issues.mod
DialoguePlus.mod
Dialogue Expanded.mod
AnimationOverhaul.mod
jrpg_vanilla.mod
Transparent UI.mod
Tranparent_small.mod
Thrasher_Warbot.mod
Skelle Bandits 1.5a.mod
Roderick Guts Dragon Slayer.mod
Provincial_Ronin.mod
PAK_Unit.mod
Sailback.mod
Oni.mod
No Cut Efficiency.mod
Nice Map [Grid + Zones + Zone names + Roads].mod
New Cities_Han.mod
MorePlate.mod
More Names.mod
More Merc Contract Options.mod
Mediocre Hairstyles.mod
Mediocre Faces.mod
Mediocre Black Armor.mod
Martial Arts Rebalanced.mod
LtEast's Rarity Backgrounds.mod
LtEast's Mod Settings Core.mod
Longer Drifter's Leather Pants.mod
LargerWeaponStorage.mod
Huaxia Qipao.mod
Huaxia Qipao ENG.mod
Hive Prosthetics.mod
Higher Bounties.mod
Dust.mod
Dropped Models Deluxe.mod
Diamond Edge Meitou Retexture (Fixed & Tweaked).mod
Custom Personal Armour Pack.mod
Craftable backpacks.mod
Cannibal_Expansion.mod
Breadnought's Facepaints.mod
Better Ashlands.mod
BackpacksExpanded.mod
BERSERK.mod
Auto-Doc.mod
Another Stealth Run.mod
Animal Backpacks.mod
Ancient training technology.mod
Adventurers Guild - Lore Friendly Recruitment.mod
Add Wazamono to Artifact Loot 2.mod
2PN8HiveSoldiers.mod
```
