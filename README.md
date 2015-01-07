# BregmanVamp
Light-weight VAMP plugins for audio and music analysis
For use with vamp-plugin-sdk-2.x

Michael A. Casey - Bregman Media Labs - Dartmouth College, 2015

Linux Installation:

Add this directory to the vamp-plugin-sdk-2.x directory
make clean
cp Makefile.in Makefile.in.bak
cp BregmanVamp/Makefile.in .
./configure
make
sudo cp BregmanVamp/vamp-bregman-plugins.so /usr/local/lib/vamp



