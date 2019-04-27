# MiniGBS

MiniGBS is a small .gbs file (gameboy music) player with an ncurses UI.
It's not cycle-accurate, but still sounds good for most of the files I've tested.

## Radio Branch

This branch has modifications used for the MiniGBS Radio idea that I briefly tested
on Twitch. Here's a clip of how the entire setup looked: https://i.abaines.me.uk/f99e1f66.mp4

The top right UI is a separate program, gbsradio, which is included on this branch.
The whole thing was orchestrated by an insobot module, however, which is not included here.
I might release it to the insobot repo if there's interest and I find the energy to clean
up all the hardcoded paths etc...

The changes to minigbs include:

  - UI Changes e.g. vol + notes display together
  - Track changing by sending a packet to the @minigbs\_ctrl abstract unix socket (DGRAM)
    - Packet format is "\[track\_no\] \[space\] \[path\_to\_gbs\]"
  - Launch with -c to connect to another minigbs and display the chart view in a separate terminal

## Hotkeys:
	n/→     Next song
	p/←     Prev song
	r/↑     Restart song
	space/↓ Pause/Resume
	-/=     Volume down/up
	1/2/3/4 Toggle channel
	c       Toggle Chart/Register view
	v       Toggle Note/Volume view
	esc/q   Quit
	[/]     Playback speed down/up
	backsp. Reset playback speed
	return  Go to track \#
	o       Toggle oscilloscope

## Recommended Listening:

| Game | Artist | Favourite track(s)* |
| ---- | ------ | ------------------- |
| Action Man - Search for Base X | ??? / Natsume | 19 (Moon base 1 boss) |
| Asterix & Obelix | Alberto Jose González | |
| Das Geheimnis der Happy Hippo-Insel | Stello Doussis | 0 (Title Screen) |
| Ganbare Goemon: Sarawareta Ebisumaru! | Akihiro Juuichiya | 6 (Stage 4 - Nagato) |
| Micro Machines V3 | Thomas E. Petersen | 0 & 1 |
| Mole Mania | Taro Bando | 15 (Forest Theme) |
| Ninja Gaiden Shadow | Hiroyuki Iwatsuki? | 8 (Final Boss) |
| Ottifanten Kommando Störtebeker | Stello Doussis | 4 (Der Zoo) |
| Pokemon Crystal | Junichi Masuda, Gō Ichinose | 17 (Lucky Channel radio) |
| Pokemon Red | Junichi Masuda | 22 (Trainer Battle) |
| Radikal Bikers prototype | Alberto Jose González | 8 (Ingame 6) |
| Shantae | Jake Kaufman | 9 (Boss) |
| Spanky's Quest | Hiroyuki Iwatsuki | 8 (Ending Credits) |
| Spider-Man | Manfred Linzner | 16 (Secret Lab) |
| Street Fighter II | Yoko Shimomura, Isao Abe | 8 (Ken's theme) |
| The Smurfs' Nightmare | Alberto Jose González | 8 (Run!) |
| Turok: Battle of the Bionosaurs | Alberto Jose González | 1 (Boss Fight) |
| Turok: Rage Wars | Alberto Jose González | 4 (BGM #5) |
| Zelda Oracle of Ages | Kiyohiro Sada, Minako Adachi | 33 (The Pirates Gigue) |

**Track numbers are zero-indexed and based on gbs from the "penultimate gbs archive" rar*

Also the included gbs/pocket.gbs file, (soundtrack to [Is That a Demo In Your Pocket?](https://www.pouet.net/prod.php?which=65997))

## Screenshots:

![Animated screenshot](/screenshots/anim.gif)
![Note display](/screenshots/notes.png)
![Chart display](/screenshots/chart.png)
![Volume display](/screenshots/volume.png)
![Oscilloscope](/screenshots/osc.png)
	
	
