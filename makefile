all : main.cc
	g++ `sdl2-config --cflags` main.cc `sdl2-config --libs` `pkg-config SDL2_ttf --libs` -o vitoothctl
