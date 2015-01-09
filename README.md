# BregmanVamp
Light-weight VAMP plugins for audio and music analysis
For use with vamp-plugin-sdk-2.x

Michael A. Casey - Bregman Media Labs - Dartmouth College, 2015

## Linux Installation:

Add this directory to the vamp-plugin-sdk-2.x directory

```
make clean
cp Makefile.in Makefile.in.bak
cp BregmanVamp/Makefile.in .
./configure
make
sudo cp BregmanVamp/vamp-bregman-plugins.so /usr/local/lib/vamp
```

## OSX Installation

### Install Homebrew packet manager:

Visit http://brew.sh/ for instructions,
or type the following into the Terminal.app application (use Spotlight to find the application "Terminal"):

`ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"`

### Install dependencies:

Open Terminal.app

type:

`brew install libvorbis --universal libsndfile --universal libogg --universal flac --universal git`

### Create plugins folder:

Open Terminal.app

type:

`mkdir ~/Library/Audio/Plug-Ins/Vamp`

### Get this repository:

Open Terminal.app

type:

```
mkdir BregmanVamp
cd BregmanVamp
git clone https://github.com/bregmanstudio/BregmanVamp .
cp vamp-bregman-plugins.dylib ~/Library/Audio/Plug-Ins/Vamp
```

### Sonic Visualizer

Make sure the plugin is installed correctly by opening [Sonic Visualizer](http://www.sonicvisualiser.org/), and going to the menu bar item "Transforms/Analysis By Maker/Bregman Media Labs/Dissonance: Linear Dissonance"

